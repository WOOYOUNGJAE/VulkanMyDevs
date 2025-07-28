/*
* Ray Tracing Basic Header
*
* Some parts of code are from sascha raytracinggltf
* Copyright (C) 2019-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once
#include "myVulkan.h"
#include "myStructsRT.h"
#include "myVulkanRTBase.h"
#include "myglTFModel.h"

#define VK_GLTF_MATERIAL_IDS
#include "myglTFModel.h"

class MyRayTracingBasic : public MyVulkanRTBase
{
public:
	AccelerationStructure TLAS{};
	AccelerationStructure BLAS{};

	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	uint32_t indexCount{ 0 };
	vks::Buffer transformBuffer;

	vks::Buffer geometryNodesBuffer;

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
	struct ShaderBindingTables {
		ShaderBindingTable raygen;
		ShaderBindingTable miss;
		ShaderBindingTable hit;
	} shaderBindingTables;

	vks::Texture2D texture;

	struct UniformData {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		uint32_t frame{ 0 };
	} uniformData;
	vks::Buffer uniformBuffer;

	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	myglTF::Model model;

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
public:
	MyRayTracingBasic();
	~MyRayTracingBasic();

	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);

	/*
		Create the bottom level acceleration structure that contains the scene's actual geometry (vertices, triangles)
	*/
	void createBottomLevelAccelerationStructure();

	/*
		The top level acceleration structure contains the scene's object instances
	*/
	void createTopLevelAccelerationStructure();

	/*
		Create the Shader Binding Tables that binds the programs and top-level acceleration structure

		SBT Layout used in this sample:

			/-----------\
			| raygen    |
			|-----------|
			| miss + shadow     |
			|-----------|
			| hit + any |
			\-----------/

	*/
	void createShaderBindingTables();

	/*
		Create our ray tracing pipeline
	*/
	void createRayTracingPipeline();

	/*
		Create the descriptor sets used for the ray tracing dispatch
	*/
	void createDescriptorSets();

	/*
		Create the uniform buffer used to pass matrices to the ray tracing ray generation shader
	*/
	void createUniformBuffer();

	/*
		If the window has been resized, we need to recreate the storage image and it's descriptor
	*/
	void handleResize();

	/*
		Command buffer generation
	*/
	void buildCommandBuffers();

	void updateUniformBuffers();

	void getEnabledFeatures();

	void loadAssets();

	void prepare();

	void draw();

	virtual void render();
};