#pragma once
#include <myVulkan.h>
#include <vector>
#include <string>

#include "myComputePass.h"


namespace myglTF
{
	class ModelRT;
}

class MyAnimComputePass : public MyComputePass
{
public:
	struct PushConstantData
	{
		uint64_t vertexBufferDeviceAddress = 0; // t-pose
		uint64_t jointDataBufferDeviceAddress = 0; // joint data (matrices, num joints,,,)
	}pushConstantData;

	/*VkDescriptorBufferInfo vertexDescriptorBufferInfo{};
	VkDescriptorBufferInfo tPoseDescriptorBufferInfo{};
	VkDescriptorBufferInfo animationDescriptorBufferInfo{};*/

	VkDescriptorSetLayout modelDescriptorSetLayoutUbo{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> modelDescriptorSets;

	VkBuffer animSsboBuffer{ VK_NULL_HANDLE };

	uint32_t numTotalVertices{ 0 };

public:
	void createDescriptorSets(myglTF::ModelRT& model);
	/**
	 * @param shaderFileName Full Shader Path
	 */
	void createPipeline(const std::string& shaderFileName);
	/**
	 * @param commandBuffer already began, and will be closed outside
	 */
	void buildCommandBuffer(VkCommandBuffer commandBuffer);
};

