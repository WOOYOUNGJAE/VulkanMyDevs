#pragma once
#include "myDefines.h"
#include "VulkanDevice.h"
#include "glm/glm.hpp"
//#include "myglTFModel.h"

// A cube mesh with one face(Z) open
class OpenCubeMesh
{
public:
	OpenCubeMesh() = default;
	~OpenCubeMesh();
	void init(glm::vec3 min, glm::vec3 max, vks::VulkanDevice* vksDevice, VkQueue transferQueue);

	struct Buffer
	{
		uint32_t count = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		uint64_t deviceAddress = 0;
		VkDeviceSize size = 0;
		VkDescriptorBufferInfo descriptor{};
	}vertexBuffer{}, indexBuffer{};
	VkDevice device = VK_NULL_HANDLE;

	glm::vec4 color = { 0.4, 1.f, 0.8f, 1.f };
	glm::vec3 worldMin{FLT_MAX, FLT_MAX , FLT_MAX };
	glm::vec3 worldMax{-FLT_MAX , -FLT_MAX , -FLT_MAX };
};

