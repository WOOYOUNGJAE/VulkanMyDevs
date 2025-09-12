#pragma once
#include "myAnimComputePass.h"
#include "myRayTracingLittleAdvanced.h"

/**
 * If Timer On, build "build accel" command each frame in render() func
 */
class MySkeletalAnimationRT : public MyVulkanRTBase
{
private:
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
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
		uint32_t bRenderTriangle = 1;
	}pushConstantData;

	// RT Pipeline
	VkPipeline rtPipeline{ VK_NULL_HANDLE };
	VkPipelineLayout rtPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet rtDescriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout rtDescriptorSetLayout{ VK_NULL_HANDLE };

	// Compute Pipeline
	std::unique_ptr<MyAnimComputePass> animComputePass;
	VkPipeline computePipeline{ VK_NULL_HANDLE };
	VkPipelineLayout computePipelineLayout{ VK_NULL_HANDLE };

	myglTF::ModelRT model;
#if ACCEL_BUILD_TIMER_ON
private:
	std::vector<std::unique_ptr<GPUTimer>> gpuTimers;
#endif
public:
	MySkeletalAnimationRT();
	~MySkeletalAnimationRT() override;

	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo, VkBufferUsageFlagBits usageFlag);

	// Create class buffer and initialize indirect build info, but don't build on GPU yet.
	/*
		Create the bottom level acceleration structure that contains the scene's actual geometry (vertices, triangles)
	*/
	// Only Called once after model loaded
	void initBLASes();
	void initTLAS();

	void buildBLASes(VkCommandBuffer cmdBuffer); // Build or Update
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