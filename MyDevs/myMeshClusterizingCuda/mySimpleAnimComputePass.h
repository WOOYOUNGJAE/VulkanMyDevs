#pragma once
#include "myComputePass.h"
#include "myDefines.h"
#include "mySimpleGltfLoader.h"


class MySimpleAnimComputePass : public MyComputePass
{
public:
	MySimpleAnimComputePass(VkDevice device) : MyComputePass(device) {}
	~MySimpleAnimComputePass() override = default;
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
	void createDescriptorSets(myglTF::MySimpleGltfLoader::Model& model, BufferSet& deformingVertexBuffer);
	/**
	 * @param shaderFileName Full Shader Path
	 */
	void createPipeline(const std::string& shaderFileName);
	/**
	 * @param commandBuffer already began, and will be closed outside
	 */
	void buildCommandBuffer(VkCommandBuffer commandBuffer);
};



class MyTwistComputePass : public MyComputePass
{
public:
	MyTwistComputePass(VkDevice device) : MyComputePass(device) {}
	~MyTwistComputePass() override = default;
	struct PushConstantData
	{
		uint64_t vertexBufferDeviceAddress = 0; // t-pose
		uint32_t vertexStartOffset = 0; // from total scene vertices
		uint32_t numVertices = 0; // current

		float curTime;
		float bboxLength;
		float twistMaxAngle;
		float twistSpeed;
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
	void createDescriptorSets(myglTF::MySimpleGltfLoader::Model& model, BufferSet& deformingVertexBuffer);
	/**
	 * @param shaderFileName Full Shader Path
	 */
	void createPipeline(const std::string& shaderFileName);
	/**
	 * @param commandBuffer already began, and will be closed outside
	 * @param curTime
	 */
	void buildCommandBuffer(VkCommandBuffer commandBuffer, float curTime);
};

