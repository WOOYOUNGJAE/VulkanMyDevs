#pragma once

#include "myIncludesCPUGPU.h"
#include <myDefines.h>

// Original Sashca Style
struct GeometryNodePerPrimitiveRT
{
	uint64_t vertexBufferDeviceAddress;
	uint64_t indexBufferDeviceAddress; 
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};


struct GeometryNodePerMeshRT
{
	uint32_t vertexStartOffset; // from scene's total vertex buffer
	uint32_t indexStartOffset; // from scene's total Index buffer
	// primitive contains material info
	// Access like AllPrimitives[primitiveStartOffset + gl_GeometryIndexEXT]
	uint32_t primitiveStartOffset;
	float customData;// padding
};

struct MeshPrimitive
{
	uint32_t vertexStartOffsetInMesh;
	uint32_t IndexStartOffsetInMesh;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};

// Extended version of GeometryNodeRT for CLAS
typedef ClusteredGeometryData ClusteredGeometryNodeRT;


struct BBox
{
	glm::vec3 min;
	glm::vec3 max;
};

// Vulkan
struct BufferSet
{
	BufferSet() = default;
	~BufferSet()
	{
		vkDestroyBuffer(device, vkBuffer, nullptr);
		vkFreeMemory(device, vkMemory, nullptr);
	}
	VkDevice device = VK_NULL_HANDLE;
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkMemory = VK_NULL_HANDLE;
	VkDescriptorBufferInfo descriptor;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	uint64_t deviceAddress = 0;
	void* mapped;
};