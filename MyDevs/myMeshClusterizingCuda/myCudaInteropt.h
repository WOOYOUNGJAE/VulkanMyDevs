#pragma once
#include "myDefines.h"
#include "myStructs.h"
#include <driver_types.h>

class MyCudaInteropt
{
public:
	MyCudaInteropt(VkPhysicalDevice _physicalDevice, VkDevice _device) : physicalDevice(_physicalDevice), device(_device){}
	~MyCudaInteropt() = default;
public:
	void createExternalBuffer(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkExternalMemoryHandleTypeFlagsKHR extMemHandleType, VkDeviceSize size, BufferSet* outBufferSet, VkQueue transferQueue = VK_NULL_HANDLE, void* data = nullptr);
	void importCudaExternalMemory(void** cudaPtr, cudaExternalMemory_t& cudaMem, VkDeviceMemory& vkMem, VkDeviceSize size, VkExternalMemoryHandleTypeFlagBits handleType);
	void* getMemoryWinHandle(VkDeviceMemory memory, VkExternalMemoryHandleTypeFlagBits handleType);
	// Index External Buffer
	// Index External DeviceMemory
	// mapped Device Ptr
private:
	VkPhysicalDevice physicalDevice;
	VkDevice device;
};

#ifdef _WIN64
class WindowsSecurityAttributes
{
protected:
    SECURITY_ATTRIBUTES  winSecurityAttributes;
    PSECURITY_DESCRIPTOR winPSecurityDescriptor;

public:
    WindowsSecurityAttributes();
    SECURITY_ATTRIBUTES* operator&() { return &winSecurityAttributes; }
    ~WindowsSecurityAttributes();
};
#endif /* _WIN64 */
