#pragma once

#include <myVulkan.h>
#include <vector>
#include <string>

#include "VulkanDevice.h"

// base compute pass class
class MyComputePass
{
protected:
	VkDevice device{ VK_NULL_HANDLE };
public:
	VkQueue queue{ VK_NULL_HANDLE };
	VkCommandPool commandPool{ VK_NULL_HANDLE };
	//VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
	VkSemaphore semaphore{ VK_NULL_HANDLE };
	VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet computeDescriptorSet{};


	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	std::vector<VkShaderModule> shaderModules;
	VkPipeline pipeline{};
public:
	MyComputePass(VkDevice device) : device(device){}
	~MyComputePass();
	/**
	 * @param shaderFileName Full Shader Path
	 */
};


