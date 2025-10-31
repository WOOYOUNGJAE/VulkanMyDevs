/*
* Ray Tracing Basic Header
*
* Some parts of code are from sascha raytracinggltf
* Copyright (C) 2019-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once
#include "myDefines.h"
#include "myVulkanRTBase.h"
#include "myglTFModel.h"

#define VK_GLTF_MATERIAL_IDS
#include "myBlasUpdateCompute.h"
#include "myglTFModel.h"

class MyClusterAccelerationStructureNV : public MyVulkanRTBase
{
private: // NV Cluster Acceleration Structure extensions
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
	VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT physicalDeviceShaderImageAtomicInt64Features{};
	VkPhysicalDeviceClusterAccelerationStructureFeaturesNV clustersNV = {
	  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
	// pfns
	//PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = VK_NULL_HANDLE;
	PFN_vkGetClusterAccelerationStructureBuildSizesNV vkGetClusterAccelerationStructureBuildSizesNV = VK_NULL_HANDLE;
	PFN_vkCmdBuildClusterAccelerationStructureIndirectNV vkCmdBuildClusterAccelerationStructureIndirectNV = VK_NULL_HANDLE;
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
	AccelerationStructure TLAS{};
	std::vector<VkAccelerationStructureInstanceKHR> blasInstances{};
public: // CLAS
	uint32_t positionTruncateBits = 0;
	VkDeviceSize clasScratchSizeMax = 0;
	// for indirectly building clas argment
	ArgumentBuffer clusterBuildInfoBuffer{}, clusterDstAddressBuffer{}, clusterSizeBuffer{},
	clusteredBlasBuildInfoBuffer{}, clusteredBlasDstAddressBuffer{}, clusteredBlasSizeBuffer{};
	VkClusterAccelerationStructureTriangleClusterInputNV clasInput{};
	ScratchBuffer clasScratchBuffer{};
	VkClusterAccelerationStructureClustersBottomLevelInputNV clusteredBlasInput	{};
	VkDeviceSize clusteredBlasScratchSizeMax = 0;
	ScratchBuffer clusteredBlasScratchBuffer{};
	// AS
	std::vector<AccelerationStructure> staticBLASes, dynamicBLASes;
	AccelerationStructure CLAS{};
	AccelerationStructure clusteredBLASes{};

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

	struct PushConstantData
	{
		uint64_t sceneVertexBufferDeviceAddress = 0;
		uint64_t sceneIndexBufferDeviceAddress = 0;
	}pushConstantData;

	// RT Pipeline
	VkPipeline rtPipeline{ VK_NULL_HANDLE };
	VkPipelineLayout rtPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet rtDescriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout rtDescriptorSetLayout{ VK_NULL_HANDLE };

	// Compute Pipeline
	std::unique_ptr<MyBLASUpdateCompute> blasUpdateComputePass{};

	myglTF::ModelRT model;

public:
	MyClusterAccelerationStructureNV();
	~MyClusterAccelerationStructureNV() override;

	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo, VkBufferUsageFlagBits usageFlag);

	// Create class buffer and initialize indirect build info, but don't build on GPU yet.
	void initCLASes();
	/*
		Create the bottom level acceleration structure that contains the scene's actual geometry (vertices, triangles)
	*/
	// Only Called once after model loaded
	void initBLASes();
	void initClusteredBLASes();
	void initTLAS();

	void buildCLASes(VkCommandBuffer cmdBuffer);
	void buildBLASes(); // Build or Update
	void buildClusteredBLASes(VkCommandBuffer cmdBuffer); // Build or Update
	void buildTLAS(VkCommandBuffer cmdBuffer);

	void createShaderBindingTables();
	void createRayTracingPipeline();
	void createDescriptorSets();
	void createUniformBuffer();

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
};