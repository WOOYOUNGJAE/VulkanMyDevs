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
#include "myUtils.h"
#include "myIncludesCPUGPU.h"

#define FORCE_STATIC_SCENE 0
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
	MyVulkanRTBase() { enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME); }
	~MyVulkanRTBase() override;
protected:
	// Update the default render pass with different color attachment load ops
	void setupRenderPass() override;
	void setupFrameBuffer() override;
	VkMemoryBarrier accelBuildBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
	inline void accelBuildPipelineBarrier(VkCommandBuffer cmdBuffer)
	{
		vkCmdPipelineBarrier(
			cmdBuffer,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_FLAGS_NONE,
			1, &accelBuildBarrier,
			0, nullptr,
			0, nullptr);
	}
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

	typedef struct BufferWithDeviceAddress
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceAddress deviceAddress = 0u;
		VkDeviceSize bufferSize = 0;
	}BufferWithDeviceAddress, ArgumentBuffer;

	/**
	 * @example
	 * gpuTimer.reset()
	 * gpuTimer.record()
	 * "Record On CommandBuffer Things"
	 * gpuTimer.record()
	 * float deltaTime = gpuTimer.timerResult()
	 */
	class GPUTimer
	{
	private:
		VkDevice device = VK_NULL_HANDLE;
		VkQueryPool timeStampQueryPool = VK_NULL_HANDLE;
		uint32_t curQueryIndex = 0;
		uint32_t queryCount; // before after
		VkQueryResultFlagBits queryFlag = static_cast<VkQueryResultFlagBits>(VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
		float timestampPeriodDeviceLimit = 0.f;
		std::vector<float> timerResults;
	public:
		~GPUTimer()
		{
			vkDestroyQueryPool(device, timeStampQueryPool, nullptr);
		}
		GPUTimer() = delete;
		GPUTimer(VkDevice inDevice, float inTimestampPeriodDeviceLimit, uint32_t qeuryCount) : device(inDevice), timestampPeriodDeviceLimit(inTimestampPeriodDeviceLimit), queryCount(qeuryCount) { timerResults.resize(queryCount / 2);}
		void init()
		{
			VkQueryPoolCreateInfo queryPoolInfo{};
			queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryPoolInfo.queryCount = queryCount; // before-after
			VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, nullptr, &timeStampQueryPool));
		}
		void reset(VkCommandBuffer cmdBuffer)
		{
#if MEASURE_MODE
			curQueryIndex = 0;
			vkCmdResetQueryPool(cmdBuffer, timeStampQueryPool, 0, queryCount);
#endif
		}

		/**
		 * @note If NOT MEASURE_MODE, Do Nothing.
		 */
		void record(VkCommandBuffer cmdBuffer, VkPipelineStageFlagBits pipelineStageFlag)
		{
#if MEASURE_MODE
			vkCmdWriteTimestamp(cmdBuffer, pipelineStageFlag, timeStampQueryPool, curQueryIndex++);
#endif
		}

		/**
		 * @return 0 if timer not ready
		 */
		const std::vector<float>& timerResult()
		{
			curQueryIndex = 0;
			std::vector<uint64_t> timeStampResults(queryCount, 0);
			vkGetQueryPoolResults(device, timeStampQueryPool, 0, queryCount, sizeof(uint64_t) * queryCount,
				timeStampResults.data(), sizeof(uint64_t), queryFlag);

			for (uint32_t i = 0; i < queryCount / 2; ++i) // (start, end, start, end, ,,,)
			{
				timerResults[i] = float(timeStampResults[i * 2 + 1] - timeStampResults[i * 2]) * timestampPeriodDeviceLimit / (1000000.0f);
			}
			
			return timerResults;
		}
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
	
protected:
	myUtils::GPUDebug* pGpuDebug;
	typedef MainRendererPushConstantData PushConstantDataBase;
};