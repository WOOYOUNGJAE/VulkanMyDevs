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
*/

#include "myMeshShader.h"

// global
bool g_useMeshShader = 1;
bool g_useTaskShader = 1;


/*
	Vulkan Example class
*/

MyMeshShader::MyMeshShader() : VulkanExampleBase()
{
	apiVersion = VK_API_VERSION_1_4;

	// Extensions required by mesh shading
	enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	enabledDeviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
	enabledDeviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	enabledDeviceExtensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
	enabledDeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

	// Required by VK_KHR_spirv_1_4
	enabledDeviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

	// We need to enable the mesh and task shader feature using a new struct introduced with the extension
	enabledMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	enabledMeshShaderFeatures.meshShader = VK_TRUE;
	enabledMeshShaderFeatures.taskShader = VK_TRUE;

	deviceCreatepNextChain = &enabledMeshShaderFeatures;

	title = "My MeshShader";
	camera.type = Camera::CameraType::firstperson;
	camera.flipY = true;
	camera.setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	camera.setRotation(glm::vec3(0.0f, -90.0f, 0.0f));
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
}

MyMeshShader::~MyMeshShader()
{
	if (device) {
		vkDestroyPipelineLayout(device, traditionalPipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, meshShaderPipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
		//vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.textures, nullptr);
		shaderData.buffer.destroy();
	}
}

void MyMeshShader::getEnabledFeatures()
{
	enabledFeatures.samplerAnisotropy = deviceFeatures.samplerAnisotropy;
}

void MyMeshShader::buildCommandBuffers()
{
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VkClearValue clearValues[2];
	clearValues[0].color = defaultClearColor;
	clearValues[0].color = { { 0.25f, 0.25f, 0.25f, 1.0f } };;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = width;
	renderPassBeginInfo.renderArea.extent.height = height;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
	const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

	for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
	{
		renderPassBeginInfo.framebuffer = frameBuffers[i];
		VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
		vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
		vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

		VkPipelineLayout curPipelineLayout = g_useMeshShader ? meshShaderPipelineLayout : traditionalPipelineLayout;
		PFN_vkCmdDrawMeshTasksEXT cmdDrawMeshTask = g_useMeshShader ? vkCmdDrawMeshTasksEXT : nullptr;

		// Bind sceneUBO descriptor to set 0
		vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, curPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		// POI: Draw the glTF scene
		model.draw(drawCmdBuffers[i], myglTF::RenderFlags::BindImages, curPipelineLayout, 2, cmdDrawMeshTask);

		drawUI(drawCmdBuffers[i]);
		vkCmdEndRenderPass(drawCmdBuffers[i]);
		VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
	}
}


void MyMeshShader::loadAssets()
{
	myglTF::FileLoadingFlags loadingFlag = (myglTF::FileLoadingFlags)(
		myglTF::FileLoadingFlags::PreTransformVertices | myglTF::FileLoadingFlags::PrepareTraditionalPipeline | myglTF::FileLoadingFlags::PrepareMeshShaderPipeline);
	model.loadFromFile(getAssetPath() + "models/sponza/sponza.gltf", vulkanDevice, queue, loadingFlag);
	//model.loadFromFile("D:\\MyHome\\Assets\\San_Miguel\\gltf\\San_Miguel.gltf", vulkanDevice, queue, loadingFlag);
}

void MyMeshShader::setupDescriptors()
{
	/*
		This sample uses separate descriptor sets (and layouts) for the matrices and materials (textures)
	*/

	// One ubo to pass dynamic data to the shader
	std::vector<VkDescriptorPoolSize> poolSizes = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1), // scene Info(light, viewproj,,)
	};
	// One set for matrices and one per model image/texture
	const uint32_t maxSetCount = static_cast<uint32_t>(model.textures.size()) + 1;
	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxSetCount);
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI;
	// DescriptorSet Layout for passing matrices
	// descriptorSet for Scene Global info
	{
		setLayoutBindings = {
			// Binding 0 : scene Info(light, viewproj,,)
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, 0),
		};
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene));
	}

	// Descriptor set for scene matrices
	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.scene, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
	VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &shaderData.buffer.descriptor);
	vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

}

void MyMeshShader::preparePipelines()
{
	// Layout
	// Pipeline layout uses both descriptor sets (set 0 = matrices, set 1 = material)
	std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.scene, model.descriptorSetLayoutUbo, model.descriptorSetLayoutImage };
	VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), (setLayouts.size()));
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &traditionalPipelineLayout));

	setLayouts.push_back(model.descriptorSetLayoutMeshShader);
	pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), (setLayouts.size()));
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &meshShaderPipelineLayout));

	//// We will use push constants to push the local matrices of a primitive to the vertex shader
	//VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, sizeof(glm::mat4), 0);
	//// Push constant ranges are part of the traditionalPipeline layout
	//pipelineLayoutCI.pushConstantRangeCount = 1;
	//pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

	// Pipelines
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
	VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
	const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);
	std::array<VkPipelineShaderStageCreateInfo, 2> traditionalShaderStages;
	std::array<VkPipelineShaderStageCreateInfo, 3> meshShaderStages;
	//std::array<VkPipelineShaderStageCreateInfo, 2> meshShaderStages; // TODO no task shader yet

	const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(myglTF::VertexSimple), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(myglTF::VertexSimple, pos)),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(myglTF::VertexSimple, normal)),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(myglTF::VertexSimple, uv)),
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(myglTF::VertexSimple, color)),
		vks::initializers::vertexInputAttributeDescription(0, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(myglTF::VertexSimple, tangent)),
	};
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::pipelineVertexInputStateCreateInfo(vertexInputBindings, vertexInputAttributes);

	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(traditionalPipelineLayout, renderPass, 0);
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;

	traditionalShaderStages[0] = loadShader(getShadersPath() + "myMeshShader/sceneBind.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	traditionalShaderStages[1] = loadShader(getShadersPath() + "myMeshShader/sceneBind.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	meshShaderStages[0] = loadShader(getShadersPath() + "myMeshShader/meshshader.task.spv", VK_SHADER_STAGE_TASK_BIT_EXT);
	meshShaderStages[1] = loadShader(getShadersPath() + "myMeshShader/meshshader.mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
	meshShaderStages[2] = loadShader(getShadersPath() + "myMeshShader/meshshader.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	///*TODO no task shader yet*/
	//meshShaderStages[0] = loadShader(getShadersPath() + "myMeshShader/meshshader.mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
	//meshShaderStages[1] = loadShader(getShadersPath() + "myMeshShader/meshshader.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	// POI: Instead if using a few fixed pipelines, we create one traditionalPipeline for each material using the properties of that material
	for (auto &material : model.materials) {

		// traditional pipeline
		pipelineCI.layout = traditionalPipelineLayout;
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(traditionalShaderStages.size());
		pipelineCI.pStages = traditionalShaderStages.data();


		struct MaterialSpecializationData {
			VkBool32 alphaMask;
			float alphaMaskCutoff;
		} materialSpecializationData;

		materialSpecializationData.alphaMask = (material.alphaMode == myglTF::Material::ALPHAMODE_MASK);
		materialSpecializationData.alphaMaskCutoff = material.alphaCutoff;

		// POI: Constant fragment shader material parameters will be set using specialization constants
		std::vector<VkSpecializationMapEntry> specializationMapEntries = {
			vks::initializers::specializationMapEntry(0, offsetof(MaterialSpecializationData, alphaMask), sizeof(MaterialSpecializationData::alphaMask)),
			vks::initializers::specializationMapEntry(1, offsetof(MaterialSpecializationData, alphaMaskCutoff), sizeof(MaterialSpecializationData::alphaMaskCutoff)),
		};
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(specializationMapEntries, sizeof(materialSpecializationData), &materialSpecializationData);
		traditionalShaderStages[1].pSpecializationInfo = &specializationInfo;

		// For double sided materials, culling will be disabled
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &material.traditionalPipeline));

	}

	// mesh shader pipeline
	pipelineCI.layout = meshShaderPipelineLayout;
	pipelineCI.pVertexInputState = nullptr;
	pipelineCI.pInputAssemblyState = nullptr;
	pipelineCI.stageCount = static_cast<uint32_t>(meshShaderStages.size());
	pipelineCI.pStages = meshShaderStages.data();
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &model.meshShaderPipeline));

}

void MyMeshShader::prepareUniformBuffers()
{
	VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &shaderData.buffer, sizeof(shaderData.values)));
	VK_CHECK_RESULT(shaderData.buffer.map());
}

void MyMeshShader::updateUniformBuffers()
{
	shaderData.values.projection = camera.matrices.perspective;
	shaderData.values.view = camera.matrices.view;
	shaderData.values.viewPos = camera.viewPos;
	memcpy(shaderData.buffer.mapped, &shaderData.values, sizeof(shaderData.values));
}

void MyMeshShader::prepare()
{
	VulkanExampleBase::prepare();
	vkCmdDrawMeshTasksEXT = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT"));



#if _DEBUG & !SKIP_SHADER_COMIPLE  // compile shaders
	std::string batchPath = getShadersPath() + "myMeshShader/ShaderCompile.bat";
	system(batchPath.c_str());
	std::cout << "\t...current project's shaders compile completed.\n";
#endif
	loadAssets();
	prepareUniformBuffers();
	setupDescriptors();
	preparePipelines();
	buildCommandBuffers();
	prepared = true;
}

void MyMeshShader::render()
{
	updateUniformBuffers();
	renderFrame();
}

void MyMeshShader::OnUpdateUIOverlay(vks::UIOverlay* overlay)
{
	if (overlay->header("Visibility"))
	{
		(overlay->checkBox("Use Mesh Shader", &g_useMeshShader));
	}
}

//VULKAN_EXAMPLE_MAIN()
MyMeshShader* myMeshShader;														
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (myMeshShader != NULL)
	{
		myMeshShader->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_  HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int)
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	for (int32_t i = 0; i < __argc; i++) { MyMeshShader::args.push_back(__argv[i]); };
	myMeshShader = new MyMeshShader();
	myMeshShader->initVulkan();
	myMeshShader->setupWindow(hInstance, WndProc);
	myMeshShader->prepare();
	myMeshShader->renderLoop();
	delete(myMeshShader);
	return 0;
}