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

struct AccelerationStructureBase
{
	uint64_t deviceAddress = 0;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkBuffer buffer = VK_NULL_HANDLE;
};

class myVulkanRTBase : public VulkanExampleBase
{
public:
	struct StorageImage {
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format;
	} storageImage;

	bool rayQueryOnly = false;

	ScratchBuffer createScratchBuffer(VkDeviceSize size);
	void deleteScratchBuffer(ScratchBuffer& scratchBuffer);
	void createStorageImage(VkFormat format, VkExtent3D extent);
	void deleteStorageImage();
	// Draw the ImGUI UI overlay using a render pass
	void drawUI(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer);
};


class myVulkanRTBaseKHR : public myVulkanRTBase
{
	struct AccelerationStructureKHR : AccelerationStructureBase
	{
		VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
	};

public:
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

	class ShaderBindingTable : public vks::Buffer
	{
	public:
		VkStridedDeviceAddressRegionKHR stridedDeviceAddressRegion{};
	};

	void enableExtensions();
	void createAccelerationStructure(AccelerationStructureKHR& accelerationStructure, VkAccelerationStructureTypeKHR type, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);
	void deleteAccelerationStructure(AccelerationStructureKHR& accelerationStructure);
	uint64_t getBufferDeviceAddress(VkBuffer buffer);
	VkStridedDeviceAddressRegionKHR getSbtEntryStridedDeviceAddressRegion(VkBuffer buffer, uint32_t handleCount);
	void createShaderBindingTable(ShaderBindingTable& shaderBindingTable, uint32_t handleCount);

	virtual void prepare();
};

class myVulkanRTBaseNV : public myVulkanRTBase
{

	struct AccelerationStructureKHR : AccelerationStructureBase
	{
		VkAccelerationStructureNV handle;
	};
};