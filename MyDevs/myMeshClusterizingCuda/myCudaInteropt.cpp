#include "myCudaInteropt.h"
#include <cuda_runtime_api.h>
#include "VulkanTools.h"

#ifdef _WIN64
#include <VersionHelpers.h>
#include <aclapi.h>
#include <dxgi1_2.h>
#endif /* _WIN64 */

#define CUDA_CHECK(call)                                                          \
    do {                                                                          \
        cudaError_t err = call;                                                   \
        if (err != cudaSuccess) {                                                 \
            fprintf(stderr, "CUDA Error: %s (error code %d) at %s:%d\n",          \
                    cudaGetErrorString(err), err, __FILE__, __LINE__);            \
            /*exit(EXIT_FAILURE);*/                                                   \
        }                                                                         \
    } while (0)

#define CUDA_CHECK_LAST_ERROR()                                                   \
    do {                                                                          \
        cudaError_t err = cudaGetLastError();                                     \
        if (err != cudaSuccess) {                                                 \
            fprintf(stderr, "CUDA Kernel Error: %s (error code %d) at %s:%d\n",   \
                    cudaGetErrorString(err), err, __FILE__, __LINE__);            \
            /*exit(EXIT_FAILURE);*/                                                   \
        }                                                                         \
    } while (0)

#define CUDA_SYNC_CHECK()                                                         \
    do {                                                                          \
        cudaDeviceSynchronize();                                                  \
        CUDA_CHECK_LAST_ERROR();                                                  \
    } while (0)

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return ~0;
}
void MyCudaInteropt::createExternalBuffer(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                          VkExternalMemoryHandleTypeFlagsKHR extMemHandleType, VkDeviceSize size, BufferSet* outBufferSet, VkQueue transferQueue, void* data)
{
	assert(outBufferSet != nullptr);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkExternalMemoryBufferCreateInfo externalMemoryBufferInfo = {};
    externalMemoryBufferInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalMemoryBufferInfo.handleTypes = extMemHandleType;
    bufferInfo.pNext = &externalMemoryBufferInfo;

    VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &outBufferSet->vkBuffer));


    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, outBufferSet->vkBuffer, &memRequirements);

#ifdef _WIN64
    WindowsSecurityAttributes winSecurityAttributes;

    VkExportMemoryWin32HandleInfoKHR vulkanExportMemoryWin32HandleInfoKHR = {};
    vulkanExportMemoryWin32HandleInfoKHR.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    vulkanExportMemoryWin32HandleInfoKHR.pNext = NULL;
    vulkanExportMemoryWin32HandleInfoKHR.pAttributes = &winSecurityAttributes;
    vulkanExportMemoryWin32HandleInfoKHR.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
    vulkanExportMemoryWin32HandleInfoKHR.name = (LPCWSTR)NULL;
#endif /* _WIN64 */
    VkExportMemoryAllocateInfoKHR vulkanExportMemoryAllocateInfoKHR = {};

    vulkanExportMemoryAllocateInfoKHR.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
#ifdef _WIN64
    vulkanExportMemoryAllocateInfoKHR.pNext = extMemHandleType & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
        ? &vulkanExportMemoryWin32HandleInfoKHR
        : NULL;
    vulkanExportMemoryAllocateInfoKHR.handleTypes = extMemHandleType;
#else
    vulkanExportMemoryAllocateInfoKHR.pNext = NULL;
    vulkanExportMemoryAllocateInfoKHR.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif /* _WIN64 */

    VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
   if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
       allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
       allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
       allocFlagsInfo.pNext = &vulkanExportMemoryAllocateInfoKHR;
   }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlagsInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);


    VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &outBufferSet->vkMemory));

    vkBindBufferMemory(device, outBufferSet->vkBuffer, outBufferSet->vkMemory, 0);

}

void* MyCudaInteropt::getMemoryWinHandle(VkDeviceMemory memory, VkExternalMemoryHandleTypeFlagBits handleType)
{
#ifdef _WIN64
    HANDLE handle = 0;

    VkExportMemoryAllocateInfo exportAllocInfo
	{
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
	    .pNext = NULL,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
    };

    VkMemoryGetWin32HandleInfoKHR vkMemoryGetWin32HandleInfoKHR = {};
    vkMemoryGetWin32HandleInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    vkMemoryGetWin32HandleInfoKHR.pNext = NULL;
    vkMemoryGetWin32HandleInfoKHR.memory = memory;
    vkMemoryGetWin32HandleInfoKHR.handleType = handleType;

    PFN_vkGetMemoryWin32HandleKHR fpGetMemoryWin32HandleKHR;
    fpGetMemoryWin32HandleKHR =
        (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR");
    if (!fpGetMemoryWin32HandleKHR) {
        throw std::runtime_error("Failed to retrieve vkGetMemoryWin32HandleKHR!");
    }
    if (fpGetMemoryWin32HandleKHR(device, &vkMemoryGetWin32HandleInfoKHR, &handle) != VK_SUCCESS) {
        throw std::runtime_error("Failed to retrieve handle for buffer!");
    }
    return (void*)handle;
#else
    int fd = -1;

    VkMemoryGetFdInfoKHR vkMemoryGetFdInfoKHR = {};
    vkMemoryGetFdInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    vkMemoryGetFdInfoKHR.pNext = NULL;
    vkMemoryGetFdInfoKHR.memory = memory;
    vkMemoryGetFdInfoKHR.handleType = handleType;

    PFN_vkGetMemoryFdKHR fpGetMemoryFdKHR;
    fpGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR");
    if (!fpGetMemoryFdKHR) {
        throw std::runtime_error("Failed to retrieve vkGetMemoryWin32HandleKHR!");
    }
    if (fpGetMemoryFdKHR(device, &vkMemoryGetFdInfoKHR, &fd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to retrieve handle for buffer!");
    }
    return (void*)(uintptr_t)fd;
#endif /* _WIN64 */
}

void MyCudaInteropt::importCudaExternalMemory(void** cudaPtr, cudaExternalMemory_t& cudaMem, VkDeviceMemory& vkMem,
                                              VkDeviceSize size, VkExternalMemoryHandleTypeFlagBits handleType)
{
    cudaExternalMemoryHandleDesc externalMemoryHandleDesc = {};

    if (handleType & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT) {
        externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
    }
    else if (handleType & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT) {
        externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32Kmt;
    }
    else if (handleType & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) {
        externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueFd;
    }
    else {
        throw std::runtime_error("Unknown handle type requested!");
    }

    externalMemoryHandleDesc.size = size;

#ifdef _WIN64
    externalMemoryHandleDesc.handle.win32.handle = (HANDLE)getMemoryWinHandle(vkMem, handleType);
#else
    externalMemoryHandleDesc.handle.fd = (int)(uintptr_t)getMemHandle(vkMem, handleType);
#endif

    cudaImportExternalMemory(&cudaMem, &externalMemoryHandleDesc);
    CUDA_SYNC_CHECK();

    cudaExternalMemoryBufferDesc externalMemBufferDesc = {};
    externalMemBufferDesc.offset = 0;
    externalMemBufferDesc.size = size;
    externalMemBufferDesc.flags = 0;

    cudaExternalMemoryGetMappedBuffer(cudaPtr, cudaMem, &externalMemBufferDesc);
    CUDA_SYNC_CHECK();
}


#ifdef _WIN64
WindowsSecurityAttributes::WindowsSecurityAttributes()
{
    winPSecurityDescriptor = (PSECURITY_DESCRIPTOR)calloc(1, SECURITY_DESCRIPTOR_MIN_LENGTH + 2 * sizeof(void**));
    if (!winPSecurityDescriptor) {
        throw std::runtime_error("Failed to allocate memory for security descriptor");
    }

    PSID* ppSID = (PSID*)((PBYTE)winPSecurityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
    PACL* ppACL = (PACL*)((PBYTE)ppSID + sizeof(PSID*));

    InitializeSecurityDescriptor(winPSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);

    SID_IDENTIFIER_AUTHORITY sidIdentifierAuthority = SECURITY_WORLD_SID_AUTHORITY;
    AllocateAndInitializeSid(&sidIdentifierAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, ppSID);

    EXPLICIT_ACCESS explicitAccess;
    ZeroMemory(&explicitAccess, sizeof(EXPLICIT_ACCESS));
    explicitAccess.grfAccessPermissions = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL;
    explicitAccess.grfAccessMode = SET_ACCESS;
    explicitAccess.grfInheritance = INHERIT_ONLY;
    explicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    explicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    explicitAccess.Trustee.ptstrName = (LPTSTR)*ppSID;

    SetEntriesInAcl(1, &explicitAccess, NULL, ppACL);

    SetSecurityDescriptorDacl(winPSecurityDescriptor, TRUE, *ppACL, FALSE);

    winSecurityAttributes.nLength = sizeof(winSecurityAttributes);
    winSecurityAttributes.lpSecurityDescriptor = winPSecurityDescriptor;
    winSecurityAttributes.bInheritHandle = TRUE;
}

WindowsSecurityAttributes::~WindowsSecurityAttributes()
{
    PSID* ppSID = (PSID*)((PBYTE)winPSecurityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
    PACL* ppACL = (PACL*)((PBYTE)ppSID + sizeof(PSID*));

    if (*ppSID) {
        FreeSid(*ppSID);
    }
    if (*ppACL) {
        LocalFree(*ppACL);
    }
    free(winPSecurityDescriptor);
}
#endif /* _WIN64 */
