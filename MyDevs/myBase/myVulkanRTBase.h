#pragma once
/*
* Ray Tracing Base Header
*
* Some parts of code are from sascha raytracinggltf
* Copyright (C) 2019-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "myVulkanBase.h"

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

class MyVulkanRTBase : public MyVulkanBase
{
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

	
	VkDeviceSize totalBlasSize = 0;
	
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