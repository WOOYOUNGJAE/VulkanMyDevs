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
#include "myVulkanRTBase.h"
#include "myglTFModel.h"

#define VK_GLTF_MATERIAL_IDS
#include "myglTFModel.h"

class MyClusterAccelerationStructureNV : public MyVulkanRTBase
{
private: // NV Cluster Acceleration Structure extensions
	VkPhysicalDeviceClusterAccelerationStructureFeaturesNV clustersNV = {
	  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
	PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = VK_NULL_HANDLE;
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
public:
	struct ASBuildInfo
	{
		VkDeviceSize asSize;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos{};
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pBuildRangeInfos{};

		std::vector<VkAccelerationStructureGeometryKHR> asGeometries{};
		VkAccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo{};

		VkAccelerationStructureBuildSizesInfoKHR asBuildSizesInfo{};
	};
	AccelerationStructure TLAS{};
	VkDeviceSize tlasScratchSize = 0;
	ScratchBuffer tlasScratchBuffer{};
	vks::Buffer blasInstancesBuffer;
	VkAccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{};
	VkAccelerationStructureGeometryKHR tlasGeometry{};
	std::vector<VkAccelerationStructureInstanceKHR> blasInstances{};

	std::vector<AccelerationStructure> BLASes;
	VkDeviceSize blasScratchSizeMax = 0;
	ScratchBuffer blasesScratchBuffer{};
	std::vector<ASBuildInfo> asBuildInfos;

#pragma region CLAS
	VkDeviceSize clasScratchSizeMax = 0;
	VkClusterAccelerationStructureTriangleClusterInputNV clasTriangleClusterInput{};
#pragma endregion CLAS

	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	uint32_t indexCount{ 0 };
	vks::Buffer transformBuffer;


	vks::Buffer primitivesBuffer;

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

	struct PushConstantData
	{
		uint64_t sceneVertexBufferDeviceAddress = 0;
		uint64_t sceneIndexBufferDeviceAddress = 0;
	}pushConstantData;

	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	myglTF::ModelRT model;

public:
	MyClusterAccelerationStructureNV();
	~MyClusterAccelerationStructureNV() override;

	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);

	// Create class buffer and initialize indirect build info, but don't build on GPU yet.
	void initCLASes();
	/*
		Create the bottom level acceleration structure that contains the scene's actual geometry (vertices, triangles)
	*/
	// Only Called once after model loaded
	void initBLASes();
	void buildBLASes(); // Bvuil or Update

	/*
		The top level acceleration structure contains the scene's object instances
	*/
	void buildTLAS();

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

	void enableExtensions() override;
	void prepare() override;

	void draw();

	virtual void render();
};