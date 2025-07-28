/*
* Vulkan Example - Scene rendering
*
* Copyright (C) 2020-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Render a complete scene loaded from an glTF file. The sample is based on the glTF model loading sample,
* and adds data structures, functions and shaders required to render a more complex scene using Crytek's Sponza model.
*
* This sample comes with a tutorial, see the README.md in this folder
*//*

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"*/
#pragma once
#include "myIncludes.h"
#include "vulkanexamplebase.h"
#include "myglTFModel.h"

class MyMeshShader : public VulkanExampleBase
{
public:
	//VulkanglTFScene glTFScene; // materials contain pipeline
	myglTF::Model model;
	VkPipeline globalPipeline;

	struct ShaderData {
		vks::Buffer buffer;
		struct Values {
			glm::mat4 projection;
			glm::mat4 view;
			glm::vec4 lightPos = glm::vec4(0.0f, 2.5f, 0.0f, 1.0f);
			glm::vec4 viewPos;
		} values;
	} shaderData;

	VkPipelineLayout traditionalPipelineLayout{ VK_NULL_HANDLE };
	VkPipelineLayout meshShaderPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout scene{ VK_NULL_HANDLE };
		//VkDescriptorSetLayout textures{ VK_NULL_HANDLE };
	} descriptorSetLayouts;

	// Extensions
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT{ VK_NULL_HANDLE };
	VkPhysicalDeviceMeshShaderFeaturesEXT enabledMeshShaderFeatures{ };

	MyMeshShader();
	~MyMeshShader();
	virtual void getEnabledFeatures();
	void buildCommandBuffers();
	void loadAssets();
	void makeMeshlets();
	void setupDescriptors();
	void preparePipelines();
	void prepareUniformBuffers();
	void updateUniformBuffers();
	void prepare();
	virtual void render();
	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay);
};
