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
		uint32_t vertexStartOffset = 0; // from total scene vertices
		uint32_t numVertices = 0; // current
	}pushConstantData;

	VkDescriptorSetLayout modelDescriptorSetLayoutUbo{ VK_NULL_HANDLE };
	// dispatch per mesh
	struct DispatchSet
	{
		uint32_t vertexStartOffset = 0; // from total scene vertices
		VkDescriptorSet descriptorSet;
		uint32_t numVertices;
	};
	std::vector<DispatchSet> modelDispatchSets;
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

class MyBakedAnimComputePass : public MyComputePass
{
public:
	struct PushConstantData
	{
		uint64_t vertexBufferDeviceAddress = 0; // t-pose
		uint32_t vertexStartOffset = 0; // from total scene vertices
		uint32_t frame = 0;
	}pushConstantData;

	VkDescriptorSetLayout modelDescriptorSetLayoutUbo{ VK_NULL_HANDLE };
	// dispatch per mesh
	struct DispatchSet
	{
		uint32_t vertexStartOffset = 0; // from total scene vertices
		uint32_t numVertices;
	};
	std::vector<VkDescriptorSet> bakedAnimDescriptorSets;
	std::vector<DispatchSet> modelDispatchSets;
	uint32_t numTotalVertices{ 0 };
	uint32_t numAnimsPerFrame = 0;

public:
	void createDescriptorSets(myglTF::ModelRT& model);
	/**
	 * @param shaderFileName Full Shader Path
	 */
	void createPipeline(const std::string& shaderFileName);
	/**
	 * should be built each frame, becausae of updating anim frame number
	 * @param commandBuffer already began, and will be closed outside
	 */
	void buildCommandBuffer(VkCommandBuffer commandBuffer, uint32_t frame);
};
