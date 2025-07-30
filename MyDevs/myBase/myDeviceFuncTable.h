#pragma once
#include "myVulkan.h"
/**
 * Singleton Class that contains VKAPI_PTR (vkCmdTraceRaysKHR, ,,,)
 * for using vkFunction out of app class
 */
class MyDeviceFuncTable
{
public:
	MyDeviceFuncTable() = delete;
	MyDeviceFuncTable(MyDeviceFuncTable&) = delete;
	MyDeviceFuncTable(VkDevice device);
	~MyDeviceFuncTable() = default;


	static MyDeviceFuncTable* Get()
	{
		return m_pInstance;
	}
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = VK_NULL_HANDLE;
	// RT
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
	// NV MegaGeometry
private:
	static MyDeviceFuncTable* m_pInstance;
	VkDevice m_device = VK_NULL_HANDLE;
};

