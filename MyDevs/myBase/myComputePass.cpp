#include "myComputePass.h"

MyComputePass::~MyComputePass()
{
	for (auto& shaderModule : shaderModules)
	{
		vkDestroyShaderModule(device, shaderModule, nullptr);
	}

	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
}