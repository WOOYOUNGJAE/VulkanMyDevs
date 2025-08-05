#pragma once
/*
* Ray Tracing Base Header
*
* Some parts of code are from sascha raytracinggltf
* Copyright (C) 2019-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"

struct ScratchBuffer
{
	uint64_t deviceAddress = 0;
	VkBuffer handle = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct AccelerationStructure
{
	uint64_t deviceAddress = 0;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
};

class MyVulkanRTBase : public VulkanExampleBase
{
private:
	class MyDeviceFuncTable* deviceFuncTable = nullptr;
protected:
	~MyVulkanRTBase() override;
protected:
	// Update the default render pass with different color attachment load ops
	void setupRenderPass() override;
	void setupFrameBuffer() override;
public:
	class ShaderBindingTable : public vks::Buffer
	{
	public:
		VkStridedDeviceAddressRegionKHR stridedDeviceAddressRegion{};
	};
	struct StorageImage {
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format;
	} storageImage;

	struct GPUTimer
	{
		VkQueryPool timeStampQueryPool = VK_NULL_HANDLE;
		std::array<uint64_t, 2> resultPrevCur{};
		uint32_t queryFlagCount = 2;
		VkQueryResultFlagBits queryFlag = static_cast<VkQueryResultFlagBits>(VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

		GPUTimer() = delete;
		GPUTimer(VkDevice inDevice, float inTimestampPeriodDeviceLimit) : device(inDevice), timestampPeriodDeviceLimit(inTimestampPeriodDeviceLimit) {}
		void init(const uint32_t queryFlagCount)
		{
			VkQueryPoolCreateInfo queryPoolInfo{};
			this->queryFlagCount = queryFlagCount;
			queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryPoolInfo.queryCount = queryFlagCount;
			VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, nullptr, &timeStampQueryPool));
		}
		void reset(VkCommandBuffer cmdBuffer)
		{
			vkCmdResetQueryPool(cmdBuffer, timeStampQueryPool, 0, queryFlagCount);
		}
		void record(VkCommandBuffer cmdBuffer, VkPipelineStageFlagBits pipelineStageFlag, uint32_t queryIndex = 0)
		{
			vkCmdWriteTimestamp(cmdBuffer, pipelineStageFlag, timeStampQueryPool, queryIndex);
		}

		/**
		 * @return -1 if timer not ready
		 */
		float timerResult()
		{
			float result = -1.f;
			uint64_t timeStampResult[2]{};
			vkGetQueryPoolResults(device, timeStampQueryPool, 0, 1, sizeof(timeStampResult),
				timeStampResult, sizeof(timeStampResult), queryFlag);

			if (timeStampResult[1]) // availability
			{
				resultPrevCur[1] = timeStampResult[0];
				result = float(resultPrevCur[1] - resultPrevCur[0]) * timestampPeriodDeviceLimit / (1000000.0f);
				resultPrevCur[0] = resultPrevCur[1];
			}

			return result;
		}
	private:
		VkDevice device = VK_NULL_HANDLE;
		float timestampPeriodDeviceLimit = 0.f;
	};
	std::unique_ptr<GPUTimer> gpuTimer;

	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = VK_NULL_HANDLE;
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = VK_NULL_HANDLE;
	PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = VK_NULL_HANDLE;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = VK_NULL_HANDLE;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = VK_NULL_HANDLE;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = VK_NULL_HANDLE;
	PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = VK_NULL_HANDLE;
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = VK_NULL_HANDLE;
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = VK_NULL_HANDLE;
	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = VK_NULL_HANDLE;

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rayTracingPipelineProperties{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

	VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};

	bool rayQueryOnly = false;
	void createStorageImage(VkFormat format, VkExtent3D extent);
	void deleteStorageImage();
	// Draw the ImGUI UI overlay using a render pass
	void drawUI(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer);
	ScratchBuffer createScratchBuffer(VkDeviceSize size);
	void deleteScratchBuffer(ScratchBuffer& scratchBuffer);
	void createAccelerationStructure(AccelerationStructure& accelerationStructure, VkAccelerationStructureTypeKHR type, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);
	void deleteAccelerationStructure(AccelerationStructure& accelerationStructure);
	uint64_t getBufferDeviceAddress(VkBuffer buffer);
	VkStridedDeviceAddressRegionKHR getSbtEntryStridedDeviceAddressRegion(VkBuffer buffer, uint32_t handleCount);
	void createShaderBindingTable(ShaderBindingTable& shaderBindingTable, uint32_t handleCount);
	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);


	virtual void enableExtensions();
	virtual void prepare();
};