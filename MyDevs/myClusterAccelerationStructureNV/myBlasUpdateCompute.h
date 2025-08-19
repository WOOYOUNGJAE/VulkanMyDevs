#pragma once
#include "myComputePass.h"
#include "myIncludesCPUGPU.h"

class MyBLASUpdateCompute : public MyComputePass
{
public:
	/**
	 * @param shaderFileName Full Shader Path
	 */
	void createPipeline(const std::string& shaderFileName)
	{
		VkShaderModule shaderModule;
		// Push constant - 
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(ClusteredBlasPushConstantData);

		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);
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
	/**
	 * @param cmdBuffer already began, and will be closed outside
	 * @param pushData made from outside
	 */
	void buildCommandBuffer(VkCommandBuffer cmdBuffer, const ClusteredBlasPushConstantData& pushData)
	{
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

		vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			sizeof(ClusteredBlasPushConstantData), &pushData);
		vkCmdDispatch(cmdBuffer, (std::max(pushData.instanceCount, pushData.sumCount) + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
			1, 1);
	}
};