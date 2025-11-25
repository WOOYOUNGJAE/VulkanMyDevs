#pragma once
#include "myDefines.h"
#include "myVulkanRTBase.h"
#define VK_GLTF_MATERIAL_IDS
#include "myAnimComputePass.h"
#include "mySimpleGltfLoader.h"
#include "openCubeMesh.h"
#include <cuda_runtime_api.h>

namespace gmcCuda
{
	class ClusterBuilder;
}

/**
 * If Timer On, build "build accel" command each frame in render() func
 */
class MyMeshClusterizingCuda : public MyVulkanRTBase
{
private:
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
	std::unique_ptr<gmcCuda::ClusterBuilder> clusterBuilder;
	std::unique_ptr<class MyCudaInteropt> cudaInteropt;
	BufferSet externalIndexBuffer{};
	cudaExternalMemory_t cudaMem = nullptr;
	float* d_indexBuffer = nullptr;
public:	// BLAS
	struct PerBLASBuildInfo // per blas
	{
		VkDeviceSize blasScratchSizeMax = 0;
		VkDeviceSize asSize;
		ScratchBuffer blasScratchBuffer{};
		std::vector<VkAccelerationStructureGeometryKHR> asGeometries{};

		std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos{}; // copy this ptr to ASBuildSets
		VkAccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo{}; // copy to ASBuildSets
	};
	std::vector<PerBLASBuildInfo> staticPerBlasBuildInfos, dynamicPerBlasBuildInfos;
	struct ASBuildSets // SOA
	{
		std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeometryInfos; // per blas
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangeInfosArray; // double ptr.
	}staticBlasBuildingSets{}, dynamicBlasBuildingSets{};
public: // TLAS
	BufferWithDeviceAddress blasInstancesBuffer; // for tlas
	VkAccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{};
	VkDeviceSize tlasSize;
	VkAccelerationStructureGeometryKHR tlasGeometry{};
	VkDeviceSize tlasScratchSize = 0;
	ScratchBuffer tlasScratchBuffer{};
	std::vector<VkAccelerationStructureInstanceKHR> blasInstances{};
	// AS
	std::vector<AccelerationStructure> staticBLASes, dynamicBLASes;
	AccelerationStructure TLAS{};
public: // Cluster
	struct ClusterNode
	{
#ifdef __cplusplus
		using mat4 = glm::mat4;
#endif
		mat4 worldMatrix;

		// all scene's vertex/index buff address push as pushconstant for bindless
		uint64_t vertexBufferDeviceAddress;
		uint64_t indexBufferDeviceAddress;

		uint32_t numTriangles;
		uint32_t numVertices;
		uint32_t numClusters;
		uint32_t geometryID;

		uint32_t triangleStartOffset; // from all scene's triangles
		uint32_t clusterStartOffset; // from all scene's clusters, use as allClusters[clsuterStartOffset + clusterID]
		uint32_t primitiveStartOffset; // from all scene's gltf primitives
		uint32_t padding;
	};
	std::vector<ClusterNode> clusterNodes;

	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	uint32_t indexCount{ 0 };
	vks::Buffer transformBuffer;

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
	struct ShaderBindingTables {
		ShaderBindingTable raygen;
		ShaderBindingTable miss;
		ShaderBindingTable hit;
	} shaderBindingTables;


	vks::Texture2D texture;

	struct UniformData {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		uint32_t frame{ 0 };
	} uniformData;
	vks::Buffer uniformBuffer;

	struct PushConstantData : PushConstantDataBase
	{
		float customData = 0.f;

		glm::vec4 cubeColor;
	}pushConstantData;

	struct SpecialzationData
	{
		glm::vec3 lightPos;
	}specializationData{};

	// RT Pipeline
	VkPipeline rtPipeline{ VK_NULL_HANDLE };
	VkPipelineLayout rtPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet rtDescriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout rtDescriptorSetLayout{ VK_NULL_HANDLE };

	// Compute Pipeline
	std::unique_ptr<MyAnimComputePass> animComputePass;
	std::unique_ptr<MyBindlessAnimComputePass> animBindlessPass;
	VkPipeline computePipeline{ VK_NULL_HANDLE };
	VkPipelineLayout computePipelineLayout{ VK_NULL_HANDLE };

	myglTF::MySimpleGltfLoader loader;
	myglTF::MySimpleGltfLoader::Model model;

public:
	MyMeshClusterizingCuda();
	~MyMeshClusterizingCuda() override;

	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo, VkBufferUsageFlagBits usageFlag);

	// Create class buffer and initialize indirect build info, but don't build on GPU yet.
	/*
		Create the bottom level acceleration structure that contains the scene's actual geometry (vertices, triangles)
	*/
	// Only Called once after model loaded
	void initTriangleBLASes();
	void initClusterBLASes();
	void initTLAS();

	void buildBLASes(VkCommandBuffer cmdBuffer); // not HCB, same as normal building
	void hcbBuildBLASes(VkCommandBuffer cmdBuffer); // Build or Update
	void buildTLAS(VkCommandBuffer cmdBuffer);

	void createShaderBindingTables();
	void createRayTracingPipeline();
	void createDescriptorSets();
	void createUniformBuffer();

	void createComputePipeline();

	void handleResize();

	/*
		Command buffer generation
	*/
	void buildCommandBuffers();

	void updateUniformBuffers();

	void getEnabledFeatures();

	void loadAssets();

	void enableExtensions() override;
	void prepare() override;

	void draw();

	virtual void render();

	void OnUpdateUIOverlay(vks::UIOverlay* overlay) override;
};