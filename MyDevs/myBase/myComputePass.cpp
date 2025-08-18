#include "myComputePass.h"

MyComputePass::~MyComputePass()
{
	for (auto& shaderModule : shaderModules)
	{
		vkDestroyShaderModule(device, shaderModule, nullptr);
	}
}