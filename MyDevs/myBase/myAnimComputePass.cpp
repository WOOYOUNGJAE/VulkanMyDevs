#include "myAnimComputePass.h"
#include "myglTFModel.h"


void MyAnimComputePass::createDescriptorSets(myglTF::ModelRT& model)
{
	uint32_t modelUboBinding = model.modelBufferBinding;
	uint32_t dstBinding = modelUboBinding + 1;

	// update from model
	{
		modelDescriptorSetLayoutUbo = model.descriptorSetLayoutModel;
		numTotalVertices = model.vertices.count;
		for (const auto& node : model.linearNodes)
		{
			if (node->mesh)
			{
				DispatchSet dispatchSet{};
				dispatchSet.descriptorSet = node->mesh->uniformBuffer.descriptorSet;
				dispatchSet.vertexStartOffset = node->mesh->primitives[0]->firstVertex;
				for (const auto& primitive : node->mesh->primitives)
				{
					dispatchSet.numVertices += primitive->vertexCount;
				}
				modelDispatchSets.push_back(dispatchSet);
			}

		}
		pushConstantData.vertexBufferDeviceAddress = model.vertices.deviceAddress;
	}


	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, // Deformed Vertices (Out)
		//{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // Skinning Data
	};
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);

	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));	// descriptor pool

	// Descriptor Sets
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		// Binding [dstBinding]: Deformed Vertices (Out)
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, dstBinding),
	};

	std::vector<VkDescriptorBindingFlagsEXT> descriptorBindingFlags = {
		0,
	};

	// Descriptor Set Layout
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT setLayoutBindingFlags{};
	setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	setLayoutBindingFlags.bindingCount = descriptorBindingFlags.size();
	setLayoutBindingFlags.pBindingFlags = descriptorBindingFlags.data();

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	descriptorSetLayoutCI.pNext = &setLayoutBindingFlags;
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

	// Allocate Descriptor Set
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo =
		vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &computeDescriptorSet));

	// Initialize Descriptor Set (Update)
	//animationDescriptorBufferInfo = { animSsboBuffer, 0, VK_WHOLE_SIZE };
	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.resize(1);
	{
		// Binding [dstBinding]: Deformed Vertices (Out)
		writeDescriptorSets[0] = vks::initializers::writeDescriptorSet(computeDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dstBinding, &model.deformingVertices.descriptor);
	};

	// Update
	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

}

void MyAnimComputePass::createPipeline(const std::string& shaderFileName)
{
	VkShaderModule shaderModule;
	std::vector<VkDescriptorSetLayout> setLayouts = { modelDescriptorSetLayoutUbo, descriptorSetLayout };
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstantData);

	VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), setLayouts.size());
	pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

	VkComputePipelineCreateInfo pipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayout);

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
	shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	outModule = vks::tools::loadShader(androidApp->activity->assetManager, shaderFileName.c_str(), device);
#else
	shaderModule = vks::tools::loadShader(shaderFileName.c_str(), device);
#endif
	shaderStageCreateInfo.module = shaderModule; // Destroy on VulkanRTBase
	shaderStageCreateInfo.pName = "main";
	assert(shaderStageCreateInfo.module != VK_NULL_HANDLE);

	pipelineCreateInfo.stage = shaderStageCreateInfo;

	shaderModules.push_back(shaderModule);
	VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline));
}

void MyAnimComputePass::buildCommandBuffer(VkCommandBuffer commandBuffer)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	// bind descriptorset 1
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &computeDescriptorSet, 0, 0);


	for (auto iterDispatchSet = modelDispatchSets.begin(); iterDispatchSet != modelDispatchSets.end(); ++iterDispatchSet)
	{
		pushConstantData.vertexStartOffset = iterDispatchSet->vertexStartOffset;
		pushConstantData.numVertices = iterDispatchSet->numVertices;
		vkCmdPushConstants(commandBuffer, pipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT,
			0, sizeof(PushConstantData), &pushConstantData
		);

		// bind descriptorset 0
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &iterDispatchSet->descriptorSet, 0, 0);

		//vkCmdDispatch(commandBuffer, (iterDispatchSet->numVertices), 1, 1);
		//vkCmdDispatch(commandBuffer, (iterDispatchSet->numVertices + 31) / 32, 1, 1);
		//vkCmdDispatch(commandBuffer, (iterDispatchSet->numVertices + 127) / 128, 1, 1);
		vkCmdDispatch(commandBuffer, (iterDispatchSet->numVertices + 63) / 64, 1, 1);

		// If not last element -> memory barrier
		if (iterDispatchSet != modelDispatchSets.end() - 1)
		{
			VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT };
			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_FLAGS_NONE,
				1, &memBarrier,
				0, nullptr,
				0, nullptr);			
		}
		
	}
}

//---------------------------------------------------------------------

void MyBakedAnimComputePass::createDescriptorSets(myglTF::ModelRT& model)
{
	const uint32_t bakedAnimSsboBinding = model.modelBufferBinding;
	const uint32_t dstBinding = bakedAnimSsboBinding + 1;
	numAnimsPerFrame = model.activeAnimations.size();
	// update from model
	{
		modelDescriptorSetLayoutUbo = model.descriptorSetLayoutModel;
		numTotalVertices = model.vertices.count;
		for (const auto& node : model.linearNodes)
		{
			if (node->mesh)
			{
				DispatchSet dispatchSet{};
				dispatchSet.vertexStartOffset = node->mesh->primitives[0]->firstVertex;
				for (const auto& primitive : node->mesh->primitives)
				{
					dispatchSet.numVertices += primitive->vertexCount;
				}
				modelDispatchSets.push_back(dispatchSet);
			}

		}
		pushConstantData.vertexBufferDeviceAddress = model.vertices.deviceAddress;
	}
	for (const auto& bakedUniformBuffer : model.bakedUniformBuffers)
	{
		bakedAnimDescriptorSets.push_back(bakedUniformBuffer.descriptorSet);
	}

	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, // Deformed Vertices (Out)
		//{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // Skinning Data
	};
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);

	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));	// descriptor pool

	// Descriptor Sets
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		// Binding [dstBinding]: Deformed Vertices (Out)
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, dstBinding),
	};

	std::vector<VkDescriptorBindingFlagsEXT> descriptorBindingFlags = {
		0,
	};

	// Descriptor Set Layout
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT setLayoutBindingFlags{};
	setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	setLayoutBindingFlags.bindingCount = descriptorBindingFlags.size();
	setLayoutBindingFlags.pBindingFlags = descriptorBindingFlags.data();

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	descriptorSetLayoutCI.pNext = &setLayoutBindingFlags;
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

	// Allocate Descriptor Set
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo =
		vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &computeDescriptorSet));

	// Initialize Descriptor Set (Update)
	//animationDescriptorBufferInfo = { animSsboBuffer, 0, VK_WHOLE_SIZE };
	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.resize(1);
	{
		// Binding [dstBinding]: Deformed Vertices (Out)
		writeDescriptorSets[0] = vks::initializers::writeDescriptorSet(computeDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dstBinding, &model.deformingVertices.descriptor);
	};

	// Update
	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

}

void MyBakedAnimComputePass::createPipeline(const std::string& shaderFileName)
{
	VkShaderModule shaderModule;
	std::vector<VkDescriptorSetLayout> setLayouts = { modelDescriptorSetLayoutUbo, descriptorSetLayout };
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstantData);

	VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), setLayouts.size());
	pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

	VkComputePipelineCreateInfo pipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayout);

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
	shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	outModule = vks::tools::loadShader(androidApp->activity->assetManager, shaderFileName.c_str(), device);
#else
	shaderModule = vks::tools::loadShader(shaderFileName.c_str(), device);
#endif
	shaderStageCreateInfo.module = shaderModule; // Destroy on VulkanRTBase
	shaderStageCreateInfo.pName = "main";
	assert(shaderStageCreateInfo.module != VK_NULL_HANDLE);

	pipelineCreateInfo.stage = shaderStageCreateInfo;

	shaderModules.push_back(shaderModule);
	VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline));

}

void MyBakedAnimComputePass::buildCommandBuffer(VkCommandBuffer commandBuffer, uint32_t frame)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	// bind descriptorset 1
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &computeDescriptorSet, 0, 0);

	pushConstantData.frame = frame;
	//for (auto iterDispatchSet = modelDispatchSets.begin(); iterDispatchSet != modelDispatchSets.end(); ++iterDispatchSet)
	for (uint32_t dispatchSetIdx = 0; dispatchSetIdx < modelDispatchSets.size(); ++dispatchSetIdx)
	{
		const auto& iterDispatchSet = modelDispatchSets[dispatchSetIdx];

		// bind descriptorset 0
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &bakedAnimDescriptorSets[frame * numAnimsPerFrame + dispatchSetIdx], 0, 0);

		//pushConstantData.bakedAnimBufferDeviceAddress
		pushConstantData.vertexStartOffset = iterDispatchSet.vertexStartOffset;
		vkCmdPushConstants(commandBuffer, pipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT,
			0, sizeof(PushConstantData), &pushConstantData
		);

		vkCmdDispatch(commandBuffer, (iterDispatchSet.numVertices), 1, 1);

		// If not last element -> memory barrier
		if (dispatchSetIdx < modelDispatchSets.size() - 1)
		{
			VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT };
			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_FLAGS_NONE,
				1, &memBarrier,
				0, nullptr,
				0, nullptr);
		}
	}
}
