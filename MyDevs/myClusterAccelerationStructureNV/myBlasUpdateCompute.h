#pragma once
#include "myComputePass.h"

class MyBLASUpdateCompute : public MyComputePass
{
public:
	struct PushConstantData
	{
		uint64_t vertexBufferDeviceAddress = 0; // t-pose
		uint64_t jointDataBufferDeviceAddress = 0; // joint data (matrices, num joints,,,)
	}pushConstantData;

	VkDescriptorSetLayout modelDescriptorSetLayoutUbo{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> modelDescriptorSets;

	uint32_t numTotalVertices{ 0 };

public:
	void createDescriptorSets();
	/**
	 * @param shaderFileName Full Shader Path
	 */
	void createPipeline(const std::string& shaderFileName);
	/**
	 * @param commandBuffer already began, and will be closed outside
	 */
	void buildCommandBuffer(VkCommandBuffer commandBuffer);
};
