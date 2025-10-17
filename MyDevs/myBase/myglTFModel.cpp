#include "myglTFModel.h"

#include "myDeviceFuncTable.h"
#include "myIncludesCPUGPU.h"
#include "myUtils.h"
#include "MyVulkanRTBase.h"

VkMemoryPropertyFlags myglTF::ModelRT::memoryPropertyFlags = 0;
uint32_t myglTF::ModelRT::descriptorBindingFlags = myglTF::DescriptorBindingFlags::ImageBaseColor | myglTF::DescriptorBindingFlags::ImageNormalMap;

#include "meshoptimizer.h"

static std::vector<std::pair<uint32_t, uint32_t>> arrange_ClusterID; // <vertexArrange, ClusterID>
static uint64_t globalClusterIdx;

void myglTF::Texture::updateDescriptor()
{
	descriptor.sampler = sampler;
	descriptor.imageView = view;
	descriptor.imageLayout = imageLayout;
}

void myglTF::Texture::destroy()
{
	if (device)
	{
		vkDestroyImageView(device->logicalDevice, view, nullptr);
		vkDestroyImage(device->logicalDevice, image, nullptr);
		vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
		vkDestroySampler(device->logicalDevice, sampler, nullptr);
	}
}

void myglTF::Texture::fromglTfImage(tinygltf::Image& gltfimage, std::string path, vks::VulkanDevice* device,
	VkQueue copyQueue)
{
	this->device = device;

	bool isKtx = false;
	// Image points to an external ktx file
	if (gltfimage.uri.find_last_of(".") != std::string::npos) {
		if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx") {
			isKtx = true;
		}
	}

	VkFormat format;

	if (!isKtx) {
		// Texture was loaded using STB_Image

		unsigned char* buffer = nullptr;
		VkDeviceSize bufferSize = 0;
		bool deleteBuffer = false;
		if (gltfimage.component == 3) {
			// Most devices don't support RGB only on Vulkan so convert if necessary
			// TODO: Check actual format support and transform only if required
			bufferSize = gltfimage.width * gltfimage.height * 4;
			buffer = new unsigned char[bufferSize];
			unsigned char* rgba = buffer;
			unsigned char* rgb = &gltfimage.image[0];
			for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
				for (int32_t j = 0; j < 3; ++j) {
					rgba[j] = rgb[j];
				}
				rgba += 4;
				rgb += 3;
			}
			deleteBuffer = true;
		}
		else {
			buffer = &gltfimage.image[0];
			bufferSize = gltfimage.image.size();
		}
		assert(buffer);

		format = VK_FORMAT_R8G8B8A8_UNORM;

		VkFormatProperties formatProperties;

		width = gltfimage.width;
		height = gltfimage.height;
		mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

		vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs{};

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

		uint8_t* data{ nullptr };
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
		memcpy(data, buffer, bufferSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		VkImageMemoryBarrier imageMemoryBarrier{};

		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = width;
		bufferCopyRegion.imageExtent.height = height;
		bufferCopyRegion.imageExtent.depth = 1;

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		device->flushCommandBuffer(copyCmd, copyQueue, true);

		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

		// Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
		VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		for (uint32_t i = 1; i < mipLevels; i++) {
			VkImageBlit imageBlit{};

			imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.srcSubresource.layerCount = 1;
			imageBlit.srcSubresource.mipLevel = i - 1;
			imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
			imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
			imageBlit.srcOffsets[1].z = 1;

			imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.dstSubresource.layerCount = 1;
			imageBlit.dstSubresource.mipLevel = i;
			imageBlit.dstOffsets[1].x = int32_t(width >> i);
			imageBlit.dstOffsets[1].y = int32_t(height >> i);
			imageBlit.dstOffsets[1].z = 1;

			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = i;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = mipSubRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = mipSubRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}
		}

		subresourceRange.levelCount = mipLevels;
		imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		if (deleteBuffer) {
			delete[] buffer;
		}

		device->flushCommandBuffer(blitCmd, copyQueue, true);
	}
	else {
		// Texture is stored in an external ktx file
		std::string filename = path + "/" + gltfimage.uri;

		ktxTexture* ktxTexture;

		ktxResult result = KTX_SUCCESS;
#if defined(__ANDROID__)
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		if (!asset) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		ktx_uint8_t* textureData = new ktx_uint8_t[size];
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);
		result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		delete[] textureData;
#else
		if (!vks::tools::fileExists(filename)) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif		
		assert(result == KTX_SUCCESS);

		this->device = device;
		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);
		format = ktxTexture_GetVkFormat(ktxTexture);

		// Get device properties for the requested texture format
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);

		VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
		bufferCreateInfo.size = ktxTextureSize;
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

		uint8_t* data{ nullptr };
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
		memcpy(data, ktxTextureData, ktxTextureSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		for (uint32_t i = 0; i < mipLevels; i++)
		{
			ktx_size_t offset;
			KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
			assert(result == KTX_SUCCESS);
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = i;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = std::max(1u, ktxTexture->baseWidth >> i);
			bufferCopyRegion.imageExtent.height = std::max(1u, ktxTexture->baseHeight >> i);
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = offset;
			bufferCopyRegions.push_back(bufferCopyRegion);
		}

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.layerCount = 1;

		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		device->flushCommandBuffer(copyCmd, copyQueue);
		this->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

		ktxTexture_Destroy(ktxTexture);
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxLod = (float)mipLevels;
	samplerInfo.maxAnisotropy = 8.0f;
	samplerInfo.anisotropyEnable = VK_TRUE;
	VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.layerCount = 1;
	viewInfo.subresourceRange.levelCount = mipLevels;
	VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

	descriptor.sampler = sampler;
	descriptor.imageView = view;
	descriptor.imageLayout = imageLayout;
}

myglTF::Material::~Material()
{
	if (traditionalPipeline)
		vkDestroyPipeline(device->logicalDevice, traditionalPipeline, nullptr);
}

void myglTF::Material::createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
                                           uint32_t descriptorBindingFlags)
{
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
	descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocInfo.descriptorPool = descriptorPool;
	descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
	descriptorSetAllocInfo.descriptorSetCount = 1;
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &descriptorSet));
	std::vector<VkDescriptorImageInfo> imageDescriptors{};
	std::vector<VkWriteDescriptorSet> writeDescriptorSets{};
	if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
		imageDescriptors.push_back(baseColorTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = descriptorSet;
		writeDescriptorSet.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size());
		writeDescriptorSet.pImageInfo = &baseColorTexture->descriptor;
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	if (normalTexture && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
		imageDescriptors.push_back(normalTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = descriptorSet;
		writeDescriptorSet.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size());
		writeDescriptorSet.pImageInfo = &normalTexture->descriptor;
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void myglTF::Primitive::setDimensions(glm::vec3 min, glm::vec3 max)
{
	dimensions.min = min;
	dimensions.max = max;
	dimensions.size = max - min;
	dimensions.center = (min + max) / 2.0f;
	dimensions.radius = glm::distance(min, max) / 2.0f;
}


myglTF::Mesh::Mesh(vks::VulkanDevice* device, glm::mat4 matrix)
{
	this->device = device;
	this->uniformBlock.matrix = matrix;
}

myglTF::Mesh::~Mesh()
{
	if (uniformBuffer.buffer)
	{
		vkDestroyBuffer(device->logicalDevice, uniformBuffer.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, uniformBuffer.memory, nullptr);		
	}
	for (auto primitive : primitives)
	{
		delete primitive;
	}
}

void myglTF::Mesh::createUniformBuffer(bool hasSkin)
{
	VkDeviceSize blockSize = hasSkin ? sizeof(UniformBlock) : sizeof(glm::mat4);
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		blockSize,
		&uniformBuffer.buffer,
		&uniformBuffer.memory,
		&uniformBlock));
	myUtils::GPUDebug::Get()->setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)uniformBuffer.buffer, "Mesh Node Buffer");
	VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, uniformBuffer.memory, 0, blockSize, 0, &uniformBuffer.mapped));
	uniformBuffer.descriptor = { uniformBuffer.buffer, 0, blockSize };
}

glm::mat4 myglTF::Node::localMatrix()
{
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 myglTF::Node::getMatrix()
{
	glm::mat4 m = localMatrix();
	myglTF::Node* p = parent;
	while (p) {
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void myglTF::Node::update()
{
	if (mesh) {
		glm::mat4 m = getMatrix();
		if (skin) {
			mesh->uniformBlock.matrix = m;

			// Update joint matrices
			glm::mat4 inverseTransform = glm::inverse(m);
			for (size_t i = 0; i < skin->joints.size(); i++) {
				myglTF::Node* jointNode = skin->joints[i];
				glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
				jointMat = inverseTransform * jointMat;
				mesh->uniformBlock.jointMatrix[i] = jointMat;
			}
			//mesh->uniformBlock.jointcount = (float)skin->joints.size();
			memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));

		}
		else {
			memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
		}
	}

	for (auto& child : children) {
		child->update();
	}
}

void myglTF::Node::updateJoints(glm::mat4 parentMatrix, std::array<glm::mat4, 256>& jointMatrices)
{
	// if not joint node, skip.
	if (jointNodeIndex < 0)
		return;

	glm::mat4 curNodeMatrix = localMatrix();
	glm::mat4 toRoot = parentMatrix * curNodeMatrix;

	// curjointSpace -> jointRoot
	jointMatrices[jointIndexInSkin] = toRoot;

	for (auto& child : children)
		child->updateJoints(toRoot, jointMatrices);
}

myglTF::Node::~Node()
{
	if (mesh) {
		delete mesh;
	}
	for (auto& child : children) {
		delete child;
	}
}

void myglTF::ModelRT::initClusters(std::vector<uint32_t>& originalIndices, const std::vector<glm::vec3>& vertexPositions, PerMeshClustersBuildData& perMeshClustersBuildData, const uint32_t firstIndexGlobalOffset)
{
	// Do Cluster things - Strongly influenced by https://github.com/nvpro-samples/vk_animated_clusters
	size_t minTriangles = (clusterTrianglesMax / 4) & ~3; // allow smaller clusters to be generated when that significantly improves their bounds
	size_t maxVerticesPerMeshlet = clusterVerticesMax; // Same for MeshShader
	size_t maxIndicesPerMeshlet = minTriangles; // If MeshShader:124
	float clusterMeshoptSpatialFill = 0.5f;

	std::vector<uint32_t>& refClusterLocalVertices = perMeshClustersBuildData.clusterVerticesCPU;
	std::vector<uint8_t>& refClusterLocalIndices = perMeshClustersBuildData.clusterIndicesCPU;
	std::vector<BBox>& refClusterBBoxes = perMeshClustersBuildData.clusterBBoxesCPU;
	std::vector<ClusterRT>& refClusters = perMeshClustersBuildData.clustersCPU;
	std::vector<uint32_t> copiedIndices = originalIndices;


	//meshopt_optimizeVertexCache(originalIndices.data(), originalIndices.data(), originalIndices.size(),
	//	vertexPositions.size());

	size_t numClusters = 0;
	// build geometry clusters - Use MeshOptimizer(https://github.com/zeux/meshoptimizer)
	{
		std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(originalIndices.size(), clusterVerticesMax, minTriangles));

		refClusterLocalIndices.resize(meshlets.size() * clusterTrianglesMax * 3);
		refClusterLocalVertices.resize(meshlets.size() * clusterVerticesMax);
		numClusters = meshopt_buildMeshletsSpatial(
			meshlets.data(),
			refClusterLocalVertices.data(),
			refClusterLocalIndices.data(),
			originalIndices.data(),
			originalIndices.size(),
			reinterpret_cast<const float*>(vertexPositions.data()),
			vertexPositions.size(),
			sizeof(glm::vec3),
			std::min(255u, clusterVerticesMax),
			minTriangles,
			clusterTrianglesMax,
			clusterMeshoptSpatialFill);

		if (numClusters)
		{
			refClusters.resize(numClusters);
			refClusters.shrink_to_fit();

			// Fill Cluster Data
			uint64_t clusterIdx = 0;
			for (; clusterIdx < numClusters; ++clusterIdx)
			{
				meshopt_Meshlet& meshlet = meshlets[clusterIdx];
				ClusterRT& cluster = refClusters[clusterIdx];
				cluster = {};
				cluster.numTriangles = static_cast<uint16_t>(meshlet.triangle_count);
				cluster.numVertices = static_cast<uint16_t>(meshlet.vertex_count);
				cluster.firstLocalIndex = meshlet.triangle_offset;
				cluster.firstLocalVertex = meshlet.vertex_offset;

				// fill histogram thing
				++clusterTriangleHistogram[cluster.numTriangles];
				++clusterVertexHistogram[cluster.numVertices];
			}

			ClusterRT& lastCluster = refClusters[numClusters - 1];
			refClusterLocalIndices.resize(lastCluster.firstLocalIndex + lastCluster.numTriangles * 3);
			refClusterLocalIndices.shrink_to_fit();
			refClusterLocalVertices.resize(lastCluster.firstLocalVertex + lastCluster.numVertices);
			refClusterLocalVertices.shrink_to_fit();
		}
	}

	// Fill Cluster BBoxes Data
	{
		refClusterBBoxes.resize(numClusters);
		for (uint64_t clusterIdx = 0; clusterIdx < refClusters.size(); ++clusterIdx)
		{
			ClusterRT& cluster = refClusters[clusterIdx];
			BBox bbox = { {FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX} };
			for (uint32_t vertexLocalIdx = 0; vertexLocalIdx < cluster.numVertices; ++vertexLocalIdx)
			{
				uint32_t  vertexGlobalIdx = refClusterLocalVertices[cluster.firstLocalVertex + vertexLocalIdx];
				glm::vec3 pos = vertexPositions[vertexGlobalIdx];

				bbox.min = glm::min(bbox.min, pos);
				bbox.max = glm::max(bbox.max, pos);
			}
			refClusterBBoxes[clusterIdx] = bbox;
		}
	}
	// Re-order Global(Model's) Index Array in order of Clusters
	{
		uint32_t triangleOffsetInMesh = 0u;
		for (uint64_t clusterIdx = 0; clusterIdx < refClusters.size(); ++clusterIdx)
		{
			ClusterRT& cluster = refClusters[clusterIdx];
			cluster.firstTriangle = triangleOffsetInMesh;
			triangleOffsetInMesh += cluster.numTriangles;

			// cluster : | tri0 | tri1 | "tri2" | tri3 | tri4 | ,,, | 
			for (uint32_t t = 0; t < cluster.numTriangles; ++t) // per triangle in Cluster
			{
				// cur 3 "local vertices(uint3)" of triangle in clusrter
				glm::uvec3 curLocalVerticesInAllLocals = {
					refClusterLocalIndices[cluster.firstLocalIndex + (t * 3) + 0],
					refClusterLocalIndices[cluster.firstLocalIndex + (t * 3) + 1],
					refClusterLocalIndices[cluster.firstLocalIndex + (t * 3) + 2] };

				assert(curLocalVerticesInAllLocals.x < cluster.numVertices);
				assert(curLocalVerticesInAllLocals.y < cluster.numVertices);
				assert(curLocalVerticesInAllLocals.z < cluster.numVertices);

				glm::uvec3 globalVertices = {};

				glm::uvec3 globalTriangle = {
					cluster.firstLocalVertex + curLocalVerticesInAllLocals.x,
					cluster.firstLocalVertex + curLocalVerticesInAllLocals.y,
					cluster.firstLocalVertex + curLocalVerticesInAllLocals.z };

				if (true) // !m_config.clusterDedicatedVertices from scene.cpp(https://github.com/nvpro-samples/vk_animated_clusters/blob/main/src/scene.cpp)
				{
					// need one more indirection
					globalVertices = { refClusterLocalVertices[globalTriangle.x], refClusterLocalVertices[globalTriangle.y],
									  refClusterLocalVertices[globalTriangle.z] };
				}
				else
				{
					globalVertices = {
					   +cluster.firstLocalVertex + curLocalVerticesInAllLocals.x,
					   +cluster.firstLocalVertex + curLocalVerticesInAllLocals.y,
					   +cluster.firstLocalVertex + curLocalVerticesInAllLocals.z };
				}

				// write into original Index Array
				originalIndices[(cluster.firstTriangle + t) * 3 + 0] = globalVertices.x;
				originalIndices[(cluster.firstTriangle + t) * 3 + 1] = globalVertices.y;
				originalIndices[(cluster.firstTriangle + t) * 3 + 2] = globalVertices.z;
			}
		}
	}
}

void myglTF::ModelRT::updateClustersAABB(VkQueue transferQueue)
{
#if WATCH_AABB
	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	VkBufferCopy copyRegion = {};
	copyRegion.size = vertices.size;
	vkCmdCopyBuffer(copyCmd, vertices.buffer, verticesHostVisible.buffer, 1, &copyRegion);
	device->flushCommandBuffer(copyCmd, transferQueue, true);


	for (auto& perMeshClusterData : perMeshClustersBuildDatas)
	{
		assert(perMeshClusterData.clustersCPU.size());
		for (uint32_t clusterIdx = 0; clusterIdx < perMeshClusterData.clustersCPU.size(); ++clusterIdx)
		{
			BBox prevBBox = perMeshClusterData.clusterBBoxesCPU[clusterIdx];

			ClusterRT& cluster = perMeshClusterData.clustersCPU[clusterIdx];
			BBox curBBox = { {FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX} };
			for (uint32_t vertexLocalIdx = 0; vertexLocalIdx < cluster.numVertices; ++vertexLocalIdx)
			{
				uint32_t  vertexGlobalIdx = perMeshClusterData.clusterVerticesCPU[cluster.firstLocalVertex + vertexLocalIdx];
				glm::vec3 pos = vertexPositionViewer[vertexGlobalIdx].pos;

				curBBox.min = glm::min(curBBox.min, pos);
				curBBox.max = glm::max(curBBox.max, pos);
			}

			glm::vec3 oldSize = prevBBox.max - prevBBox.min;
			glm::vec3 newSize = curBBox.max - curBBox.min;



			perMeshClusterData.clusterBBoxesCPU[clusterIdx] = curBBox;
		}
	}
#endif
}

void myglTF::ModelRT::updateGeometryNode(float* blasBuildTimes, uint32_t numBLASes)
{
#if WATCH_GEOMETRYNODE

	//static bool done = false;
	//static uint32_t globalRangeMin = 0xffff, globalRangeMax = 0;
	//if (!done)
	//{
	//	for (auto& perMeshClusterData : perMeshClustersBuildDatas)
	//	{
	//		assert(perMeshClusterData.clustersCPU.size());
	//		for (uint32_t clusterIdx = 0; clusterIdx < perMeshClusterData.clustersCPU.size(); ++clusterIdx)
	//		{
	//			uint32_t curVertexMin = 0xffff, curVertexMax = 0;
	//			ClusterRT& cluster = perMeshClusterData.clustersCPU[clusterIdx];
	//			for (uint32_t vertexLocalIdx = 0; vertexLocalIdx < cluster.numVertices; ++vertexLocalIdx)
	//			{
	//				uint32_t  vertexGlobalIdx = perMeshClusterData.clusterVerticesCPU[cluster.firstLocalVertex + vertexLocalIdx];
	//				curVertexMin = std::min(curVertexMin, vertexGlobalIdx);
	//				curVertexMax = std::max(curVertexMax, vertexGlobalIdx);
	//			}
	//			arrange_ClusterID.push_back(std::make_pair(curVertexMax - curVertexMin, globalClusterIdx++));

	//			globalRangeMin = std::min(globalRangeMin, curVertexMax - curVertexMin);
	//			globalRangeMax = std::max(globalRangeMax, curVertexMax - curVertexMin);
	//		}
	//	}
	//	//std::sort(arrange_ClusterID.begin(), arrange_ClusterID.end(), [](std::pair<uint32_t, uint32_t>& a, std::pair<uint32_t, uint32_t>& b)
	//	//	{
	//	//		return a.first < b.first;
	//	//	});
	//	//for (auto& data : arrange_ClusterID)
	//	//{
	//	//	std::cout << data.first << " " << data.second << std::endl;
	//	//}
	//}
	//done = true;
	//for (int i = 0; i < numBLASes; ++i)
	//{
	//	geometryNodeViewer[i].customData = (float)(arrange_ClusterID[i].first - globalRangeMin) / (float)(globalRangeMax - globalRangeMin);
	//}


	float timeMin = *std::min_element(blasBuildTimes, blasBuildTimes + numBLASes);
	float timeMax = *std::max_element(blasBuildTimes, blasBuildTimes + numBLASes);
	float minToMax = timeMax - timeMin;
	for (int i = 0; i < numBLASes; ++i)
	{
		geometryNodeViewer[i].customData = (blasBuildTimes[i] - timeMin) / minToMax;
	}
#endif
}

myglTF::ModelRT::~ModelRT()
{
	if (combinedMeshBuffer.buffer)
	{
		vkDestroyBuffer(device->logicalDevice, combinedMeshBuffer.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, combinedMeshBuffer.memory, nullptr);
	}

	CleanBufferMemory(primitives);
	CleanBufferMemory(geometryNodes);

	for (auto& perMeshClusterData : perMeshClustersBuildDatas)
	{
		CleanBufferMemory(perMeshClusterData.clusterVerticesGPU);
		CleanBufferMemory(perMeshClusterData.clusterIndicesGPU);
		CleanBufferMemory(perMeshClusterData.clusterBBoxesGPU);
		CleanBufferMemory(perMeshClusterData.clustersGPU);
	}
	CleanBufferMemory(vertices);
	CleanBufferMemory(deformingVertices); // if skinned mesh
	CleanBufferMemory(indices);
	CleanBufferMemory(clusters);


	vkDestroyBuffer(device->logicalDevice, representingBuffer.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, representingBuffer.memory, nullptr);

	for (auto& texture : textures) {
		texture.destroy();
	}
	for (auto& node : nodes) {
		delete node;
	}
	for (auto& skin : skins) {
		delete skin;
	}
	if (descriptorSetLayoutModel != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutModel, nullptr);
		descriptorSetLayoutModel = VK_NULL_HANDLE;
	}
	if (descriptorSetLayoutImage != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutImage, nullptr);
		descriptorSetLayoutImage = VK_NULL_HANDLE;
	}

	vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
	emptyTexture.destroy();
}

void myglTF::ModelRT::loadNode(myglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex,
	const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<VertexType*>& vertices,
	float globalscale)
{
	myglTF::Node* newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = node.skin;
	newNode->matrix = glm::mat4(1.0f);
	uint32_t numMeshVertices = 0;
	// Generate local node matrix
	glm::vec3 translation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		translation = glm::make_vec3(node.translation.data());
		newNode->translation = translation;
	}
	glm::mat4 rotation = glm::mat4(1.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::make_quat(node.rotation.data());
		newNode->rotation = glm::mat4(q);
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->matrix = glm::make_mat4x4(node.matrix.data());
		if (globalscale != 1.0f) {
			//newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
		}
	};

	// Node with children
	if (node.children.size() > 0) {
		for (auto i = 0; i < node.children.size(); i++) {
			loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertices, globalscale);
		}
	}

	// Node contains mesh data
	if (node.mesh > -1) {
		static uint32_t meshID = 0;
		uint32_t primitiveIDInMesh = 0;
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		bool hasSkin = false;
		Mesh* newMesh = new Mesh(device, newNode->matrix);
		//Mesh* newMesh = new Mesh(device, newNode->matrix, !preTransform, newNode->skin);
		newMesh->name = mesh.name;
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive& primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			glm::vec3 posMin{};
			glm::vec3 posMax{};
			// Vertices
			{
				const float* bufferPos = nullptr;
				const float* bufferNormals = nullptr;
				const float* bufferTexCoords = nullptr;
				const float* bufferColors = nullptr;
				const float* bufferTangents = nullptr;
				uint32_t numColorComponents;
				const uint16_t* bufferJoints = nullptr;
				const float* bufferWeights = nullptr;

				// Position attribute is required
				assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

				const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
				posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
				posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
				}

				if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferTexCoords = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
				{
					const tinygltf::Accessor& colorAccessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
					const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
					// Color buffer are either of type vec3 or vec4
					numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
					bufferColors = reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
				}

				if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
				{
					const tinygltf::Accessor& tangentAccessor = model.accessors[primitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& tangentView = model.bufferViews[tangentAccessor.bufferView];
					bufferTangents = reinterpret_cast<const float*>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
				}

				// Skinning
				// Joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t*>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				hasSkin |= (bufferJoints && bufferWeights);

				vertexCount = static_cast<uint32_t>(posAccessor.count);

				for (size_t v = 0; v < posAccessor.count; v++) {
					/*
					 * if skin:VertexSkiniing, else: VertexSimple
					 * allocated in here, released in "loadfromfile()"
					 * pushed into param::vertexBuffer
					 */
					VertexType* vert = hasSkin ? new VertexSkinning{} : new VertexSimple{};

					vert->pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
					//if (bool preTransform) // apply node's transform to vertices while loading
					//{
					//	vert->pos = newNode->getMatrix() * glm::vec4(vert->pos, 1.f);
					//}

					vert->normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
					vert->uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					if (bufferColors) {
						switch (numColorComponents) {
						case 3:
							vert->color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
							break;
						case 4:
							vert->color = glm::make_vec4(&bufferColors[v * 4]);
							break;
						}
					}
					else {
						vert->color = glm::vec4(1.0f);
					}
					vert->tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
					if (hasSkin)
					{
						//static_cast<VertexSkinning*>(vert)->joint0 = glm::vec4(glm::make_vec4(&bufferJoints[v * 4]));
						uint8_t* ptr = (uint8_t*)bufferJoints;
						static_cast<VertexSkinning*>(vert)->joint0 = glm::vec4(glm::make_vec4(&ptr[v * 4]));
						static_cast<VertexSkinning*>(vert)->weight0 = glm::vec4(glm::make_vec4(&bufferWeights[v * 4]));
#if CUSTOM_VERTEX
						static_cast<VertexSkinning*>(vert)->customData4.x = meshID;
						static_cast<VertexSkinning*>(vert)->customData4.y = primitiveIDInMesh;
#endif
					}
					vertices.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t* buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t* buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t* buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			Primitive* newPrimitive = new Primitive(indexStart, indexCount, primitive.material > -1 ? materials[primitive.material] : materials.back());
			newPrimitive->firstVertex = vertexStart;
			newPrimitive->vertexCount = vertexCount;
			newPrimitive->setDimensions(posMin, posMax);
			newMesh->primitives.push_back(newPrimitive);
			++primitiveIDInMesh;
			numMeshVertices += vertexCount;
		}
		++meshID;
		/*If has Skin, can decide wheater to create uniform buffer*/
		newMesh->createUniformBuffer(hasSkin);
		newMesh->numVertices = numMeshVertices;
		newNode->mesh = newMesh;
		linearMeshes.push_back(newMesh);
	}
	if (parent) {
		parent->children.push_back(newNode);
	}
	else {
		nodes.push_back(newNode);
	}
	linearNodes.push_back(newNode);
}

void myglTF::ModelRT::loadSkins(tinygltf::Model& gltfModel)
{
	for (tinygltf::Skin& source : gltfModel.skins) {
		Skin* newSkin = new Skin{};
		newSkin->name = source.name;

		// Find skeleton root node
		if (source.skeleton > -1) {
			newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
			newSkin->jointRoot = newSkin->skeletonRoot;
		}
		else // assume skin's first joint is root joint
			newSkin->jointRoot = nodeFromIndex(source.joints[0]);

		rootToMatricesMap.emplace(newSkin->jointRoot, std::array<glm::mat4, MAX_JOINTS>{});

		// Find joint nodes
		int32_t jointIndexInSkin = 0;
		for (int jointIndex : source.joints) {
			Node* node = nodeFromIndex(jointIndex);
			if (node) {
				node->jointNodeIndex = jointIndex;
				node->jointIndexInSkin = jointIndexInSkin++;
				newSkin->joints.push_back(nodeFromIndex(jointIndex));
			}
		}

		// Get inverse bind matrices from buffer
		if (source.inverseBindMatrices > -1) {
			const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
			newSkin->inverseBindMatrices.resize(accessor.count);
			memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
		}

		skins.push_back(newSkin);
	}
}

void myglTF::ModelRT::loadImages(tinygltf::Model& gltfModel, vks::VulkanDevice* device, VkQueue transferQueue)
{
	for (tinygltf::Image& image : gltfModel.images) {
		myglTF::Texture texture;
		texture.fromglTfImage(image, path, device, transferQueue);
		texture.index = static_cast<uint32_t>(textures.size());
		textures.push_back(texture);
	}
	// Create an empty texture to be used for empty material images
	createEmptyTexture(transferQueue);
}

void myglTF::ModelRT::loadMaterials(tinygltf::Model& gltfModel)
{
	for (tinygltf::Material& mat : gltfModel.materials) {
		myglTF::Material material(device);
		if (mat.values.find("baseColorTexture") != mat.values.end()) {
			material.baseColorTexture = getTexture(gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
		}
		// Metallic roughness workflow
		if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
			material.metallicRoughnessTexture = getTexture(gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
		}
		if (mat.values.find("roughnessFactor") != mat.values.end()) {
			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
		}
		if (mat.values.find("metallicFactor") != mat.values.end()) {
			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
		}
		if (mat.values.find("baseColorFactor") != mat.values.end()) {
			material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
		}
		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
			material.normalTexture = getTexture(gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
		}
		else {
			material.normalTexture = &emptyTexture;
		}
		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
			material.emissiveTexture = getTexture(gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
			material.occlusionTexture = getTexture(gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if (param.string_value == "BLEND") {
				material.alphaMode = myglTF::Material::ALPHAMODE_BLEND;
			}
			if (param.string_value == "MASK") {
				material.alphaMode = myglTF::Material::ALPHAMODE_MASK;
			}
		}
		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
		}

		materials.push_back(material);
	}
	// Push a default material at the end of the list for meshes with no material assigned
	materials.push_back(myglTF::Material(device));
}

void myglTF::ModelRT::loadAnimations(tinygltf::Model& gltfModel)
{
	for (tinygltf::Animation& anim : gltfModel.animations) {
		myglTF::Animation animation{};
		animation.name = anim.name;
		if (anim.name.empty()) {
			animation.name = std::to_string(animations.size());
		}

		// Samplers
		for (auto& samp : anim.samplers) {
			myglTF::AnimationSampler sampler{};

			if (samp.interpolation == "LINEAR") {
				sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
			}
			if (samp.interpolation == "STEP") {
				sampler.interpolation = AnimationSampler::InterpolationType::STEP;
			}
			if (samp.interpolation == "CUBICSPLINE") {
				sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
			}

			// Read sampler input time values
			{
				const tinygltf::Accessor& accessor = gltfModel.accessors[samp.input];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				float* buf = new float[accessor.count];
				memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(float));
				for (size_t index = 0; index < accessor.count; index++) {
					sampler.inputs.push_back(buf[index]);
				}
				delete[] buf;
				for (auto input : sampler.inputs) {
					if (input < animation.start) {
						animation.start = input;
					};
					if (input > animation.end) {
						animation.end = input;
					}
				}
			}

			// Read sampler output T/R/S values 
			{
				const tinygltf::Accessor& accessor = gltfModel.accessors[samp.output];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				switch (accessor.type) {
				case TINYGLTF_TYPE_VEC3: {
					glm::vec3* buf = new glm::vec3[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec3));
					for (size_t index = 0; index < accessor.count; index++) {
						sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_TYPE_VEC4: {
					glm::vec4* buf = new glm::vec4[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec4));
					for (size_t index = 0; index < accessor.count; index++) {
						sampler.outputsVec4.push_back(buf[index]);
					}
					delete[] buf;
					break;
				}
				default: {
					std::cout << "unknown type" << std::endl;
					break;
				}
				}
			}

			animation.samplers.push_back(sampler);
		}

		// Channels
		for (auto& source : anim.channels) {
			myglTF::AnimationChannel channel{};

			if (source.target_path == "rotation") {
				channel.path = AnimationChannel::PathType::ROTATION;
			}
			if (source.target_path == "translation") {
				channel.path = AnimationChannel::PathType::TRANSLATION;
			}
			if (source.target_path == "scale") {
				channel.path = AnimationChannel::PathType::SCALE;
			}
			if (source.target_path == "weights") {
				std::cout << "weights not yet supported, skipping channel" << std::endl;
				continue;
			}
			channel.samplerIndex = source.sampler;
			channel.node = nodeFromIndex(source.target_node);
			if (!channel.node) {
				continue;
			}

			animation.channels.push_back(channel);
		}

		animations.push_back(animation);
	}
}

void myglTF::ModelRT::loadFromFile(std::string filename, vks::VulkanDevice* device, VkQueue transferQueue,
	uint32_t fileLoadingFlags, float scale)
{
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF gltfContext;
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = MyDeviceFuncTable::Get()->vkGetBufferDeviceAddressKHR;
	// flag things
	const bool isGeometryNodePerPrimitive = fileLoadingFlags & myglTF::FileLoadingFlags::GeometryNodePerPrimitive;
	const bool isGeometryNodePerMesh = fileLoadingFlags & myglTF::FileLoadingFlags::GeometryNodePerMesh;
	const bool bMakeClusters = fileLoadingFlags & myglTF::FileLoadingFlags::MakeClusters;
	const bool bClusteredTriangleBLAS = fileLoadingFlags & myglTF::FileLoadingFlags::ClusteredTriangleBLAS;
	const bool bClusteredBLAS = fileLoadingFlags & (myglTF::FileLoadingFlags::ClusteredBLAS | ClusteredTriangleBLAS /*TODO TEMP*/);
	const bool bCombinedMeshBuffer = fileLoadingFlags & myglTF::FileLoadingFlags::CombinedMeshBuffer;

	auto getBufferDeviceAddress = [&](VkBuffer buffer)
	{
		VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
		bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		bufferDeviceAI.buffer = buffer;
		return vkGetBufferDeviceAddressKHR(device->logicalDevice, &bufferDeviceAI);
	};

	if (fileLoadingFlags & FileLoadingFlags::DontLoadImages) {
		gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
	}
	else {
		gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
	}
#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	size_t pos = filename.find_last_of('/');
	path = filename.substr(0, pos);

	std::string error, warning;

	this->device = device;

#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);


	std::vector<VertexType*> tempVerticesCPU;
	std::vector<uint32_t> tempIndicesCPU;
	bool isSkinningModel = gltfModel.skins.size() > 0;
	preTransform = fileLoadingFlags & FileLoadingFlags::PreTransformVertices;
	uint32_t vertexSize = isSkinningModel ? sizeof(VertexSkinning) : sizeof(VertexSimple);
	if (fileLoaded) {
		if (!(fileLoadingFlags & FileLoadingFlags::DontLoadImages)) {
			loadImages(gltfModel, device, transferQueue);
		}
		loadMaterials(gltfModel);
		const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < scene.nodes.size(); i++) {
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(nullptr, node, scene.nodes[i], gltfModel, tempIndicesCPU, tempVerticesCPU, scale);
		}

		loadSkins(gltfModel);

		

		if (gltfModel.animations.size() > 0) {
			loadAnimations(gltfModel);
		}

		for (auto& anim : animations)
		{
			activeAnimations.push_back(ActiveAnimation(anim));
		}

		// Assign skins
		for (auto node : linearNodes) {
			if (node->skinIndex > -1) {
				node->skin = skins[node->skinIndex];
			}
		}

		// update joints
		updateJoints();
		for (auto& node : nodes)
			updateNodeTransforms(node);

		// Caculate Scene Bounding Box in WorldSpace, with considering skinning
		for (const auto& node : linearNodes)
		{
			if (node->mesh)
			{
				for (const Primitive* primitive : node->mesh->primitives) 
				{
					for (uint32_t i = 0; i < primitive->vertexCount; i++) 
					{
						glm::vec3 vLocalPos = tempVerticesCPU[primitive->firstVertex + i]->pos;
						glm::vec3 vWorldPos{};
						if (node->skin)
						{
							const VertexSkinning* castedVertex = static_cast<VertexSkinning*>(tempVerticesCPU[primitive->firstVertex + i]);
							const glm::vec4 vertexJoints = castedVertex->joint0;
							const glm::vec4 vertexWeights = castedVertex->weight0;
							const glm::mat4* jointMatrices = node->mesh->uniformBlock.jointMatrix;
							glm::mat4 skinMat =
								vertexWeights.x * jointMatrices[int(vertexJoints.x)] +
								vertexWeights.y * jointMatrices[int(vertexJoints.y)] +
								vertexWeights.z * jointMatrices[int(vertexJoints.z)] +
								vertexWeights.w * jointMatrices[int(vertexJoints.w)];

							vWorldPos = (node->mesh->uniformBlock.matrix * skinMat * glm::vec4(vLocalPos, 1.f));
						}
						else
						{
							vWorldPos = node->mesh->uniformBlock.matrix * glm::vec4(vLocalPos, 1.f);
						}

						sceneBBox.min = glm::min(sceneBBox.min, vWorldPos);
						sceneBBox.max = glm::max(sceneBBox.max, vWorldPos);
					}
				}
			}			
		}
	}
	else {
		vks::tools::exitFatal("Could not load glTF file \"" + filename + "\": " + error, -1);
		return;
	}
	std::cout << sceneBBox.max.x << " " << sceneBBox.max.y << " " << sceneBBox.max.z << " \n";
	std::cout << sceneBBox.min.x << " " << sceneBBox.min.y << " " << sceneBBox.min.z << " \n";
	// Pre-Calculations for requested features
	if ((fileLoadingFlags & FileLoadingFlags::PreTransformVertices) || (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) || (fileLoadingFlags & FileLoadingFlags::FlipY)) {

		const bool preMultiplyColor = fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors;
		const bool flipY = fileLoadingFlags & FileLoadingFlags::FlipY;
		for (Node* node : linearNodes) {
			if (node->mesh) {
				const glm::mat4 localMatrix = node->getMatrix();
				for (Primitive* primitive : node->mesh->primitives) {
					for (uint32_t i = 0; i < primitive->vertexCount; i++) {
						VertexType* vertex = tempVerticesCPU[primitive->firstVertex + i];
						// Pre-transform vertex positions by node-hierarchy
						if (preTransform) {
							vertex->pos = glm::vec3(localMatrix * glm::vec4(vertex->pos, 1.0f));
							vertex->normal = glm::normalize(glm::mat3(localMatrix) * vertex->normal);
						}
						// Flip Y-Axis of vertex positions
						if (flipY) {
							vertex->pos.y *= -1.0f;
							vertex->normal.y *= -1.0f;
						}
						// Pre-Multiply vertex colors with material base color
						if (preMultiplyColor) {
							vertex->color = primitive->material.baseColorFactor * vertex->color;
						}
					}
				}
			}
		}
		// precalculate vertex tranform && skinning
		if (isSkinningModel)
		{
			for (Node* node : linearNodes)
			{
				// if not skin node
				if (node->mesh == nullptr)
				{
					node->scale = glm::vec3(1.f);
					node->rotation = glm::mat4(1.f);
					node->translation = glm::vec3(0.f);
					node->matrix = glm::mat4(1.f);
				}
			}
		}
	}

	for (auto& extension : gltfModel.extensionsUsed) {
		if (extension == "KHR_materials_pbrSpecularGlossiness") {
			std::cout << "Required extension: " << extension;
			metallicRoughnessWorkflow = false;
		}
	}
	// A vector used to store vertex data in byte form, becuase of two different type vertex(VertexSimple/VertexSkinning)
	std::vector<byte> vertexBufferByte(vertexSize * tempVerticesCPU.size());
	uint64_t byteOffset = 0;
	for (VertexType*& vertex : tempVerticesCPU)
	{
		memcpy(&vertexBufferByte[byteOffset], vertex, vertexSize);
		byteOffset += vertexSize;
	}

	// reorder index array for better cache hit
	//meshopt_optimizeVertexCache(tempIndicesCPU.data(), tempIndicesCPU.data(), tempIndicesCPU.size(), tempVerticesCPU.size());


	// build cluster preapre data
	//std::vector<ClusterRT> tempClusters; // for Total Clusters
	if (bMakeClusters)
	{
		for (auto& node : linearNodes)
		{
			if (node->mesh)
			{
				PerMeshClustersBuildData perMeshClustersBuildData{};
				if (node->skin)
					perMeshClustersBuildData.hasSkin = true;
				for (const auto& primitive : node->mesh->primitives)
				{
					perMeshClustersBuildData.numMeshIndices += primitive->indexCount;
					
				}
				assert(node->mesh->primitives[0]);
				perMeshClustersBuildData.vertexStartOffset = node->mesh->primitives[0]->firstVertex;
				perMeshClustersBuildData.indexStartOffset = node->mesh->primitives[0]->firstIndex;
				perMeshClustersBuildDatas.push_back(perMeshClustersBuildData);
			}
		}

		uint32_t numGeometries = static_cast<uint32_t>(perMeshClustersBuildDatas.size()); // == mesh count, blas count
		// Do Cluster Things - cluster per mesh

		// make only combined vertex array
		std::vector<glm::vec3> vertexPositions;
		for (auto& pVertex : tempVerticesCPU)
		{
			vertexPositions.push_back(pVertex->pos);
		}
		std::vector<uint32_t> copiedIndicesCPU = tempIndicesCPU;
		tempIndicesCPU.clear();
		tempIndicesCPU.shrink_to_fit();

		clusterTriangleHistogram.resize(clusterTrianglesMax + 1, 0);
		clusterVertexHistogram.resize(clusterVerticesMax + 1, 0);

		for (uint32_t geometryIdx = 0; geometryIdx < numGeometries; ++geometryIdx)
		{
			PerMeshClustersBuildData& refPerMeshClustersData = perMeshClustersBuildDatas[geometryIdx];

			uint32_t numIndices = refPerMeshClustersData.numMeshIndices; //indices count of original mesh
			std::vector<uint32_t> meshIndices(numIndices, 0);
			auto iterIndices = copiedIndicesCPU.begin() + refPerMeshClustersData.indexStartOffset;
			meshIndices.assign(iterIndices, iterIndices + numIndices);

			initClusters(meshIndices, vertexPositions, refPerMeshClustersData, tempIndicesCPU.size());
			// update part of original all indices to mesh's updated indices
			std::move(meshIndices.begin(), meshIndices.end(), std::back_inserter(tempIndicesCPU));

			uint32_t numClusters = refPerMeshClustersData.clustersCPU.size();

			m_numTotalClusters += numClusters;
			m_perMeshClusterMax = std::max(m_perMeshClusterMax, numClusters);

			std::move(refPerMeshClustersData.clustersCPU.begin(), refPerMeshClustersData.clustersCPU.end(), std::back_inserter(tempClusters));

#if WATCH_AABB
			refPerMeshClustersData.aabbChangeRatios.resize(numClusters);
#endif

			// TODO Not Use Yet
			//// Create Buffer & Memery
			//size_t clusterVertexBufferSize = refPerMeshClustersData.clusterVerticesCPU.size() * sizeof(uint32_t);
			//size_t clusterIndexBufferSize = refPerMeshClustersData.clusterIndicesCPU.size() * sizeof(uint8_t);
			//size_t clusterBBoxBufferSize = refPerMeshClustersData.clusterBBoxesCPU.size() * sizeof(BBox);
			//size_t clusterBufferSize = refPerMeshClustersData.clustersCPU.size() * sizeof(ClusterRT);


			//device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			//	clusterVertexBufferSize, &refPerMeshClustersData.clusterVerticesGPU.buffer, &refPerMeshClustersData.clusterVerticesGPU.memory, transferQueue, refPerMeshClustersData.clusterVerticesCPU.data());
			//refPerMeshClustersData.clusterVerticesGPU.deviceAddress = getBufferDeviceAddress(refPerMeshClustersData.clusterVerticesGPU.buffer);

			//device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			//	clusterIndexBufferSize, &refPerMeshClustersData.clusterIndicesGPU.buffer, &refPerMeshClustersData.clusterIndicesGPU.memory, transferQueue, refPerMeshClustersData.clusterIndicesCPU.data());
			//refPerMeshClustersData.clusterIndicesGPU.deviceAddress = getBufferDeviceAddress(refPerMeshClustersData.clusterIndicesGPU.buffer);

			//device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			//	clusterBBoxBufferSize, &refPerMeshClustersData.clusterBBoxesGPU.buffer, &refPerMeshClustersData.clusterBBoxesGPU.memory, transferQueue, refPerMeshClustersData.clusterBBoxesCPU.data());
			//refPerMeshClustersData.clusterBBoxesGPU.deviceAddress = getBufferDeviceAddress(refPerMeshClustersData.clusterBBoxesGPU.buffer);

			//device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			//	clusterBufferSize, &refPerMeshClustersData.clustersGPU.buffer, &refPerMeshClustersData.clustersGPU.memory, transferQueue, refPerMeshClustersData.clustersCPU.data());
			//refPerMeshClustersData.clustersGPU.deviceAddress = getBufferDeviceAddress(refPerMeshClustersData.clustersGPU.buffer);

			//// Create Descriptor Info
			//refPerMeshClustersData.clusterVerticesGPU.descriptor = { refPerMeshClustersData.clusterVerticesGPU.buffer, 0, clusterVertexBufferSize };
			//refPerMeshClustersData.clusterIndicesGPU.descriptor = { refPerMeshClustersData.clusterIndicesGPU.buffer, 0, clusterIndexBufferSize };
			//refPerMeshClustersData.clusterBBoxesGPU.descriptor = { refPerMeshClustersData.clusterBBoxesGPU.buffer, 0, clusterBBoxBufferSize };
			//refPerMeshClustersData.clustersGPU.descriptor = { refPerMeshClustersData.clustersGPU.buffer, 0, clusterBufferSize };
		}
		clusters.count = m_numTotalClusters;

		//std::vector<uint32_t> nums;
		//for (auto& mesh : perMeshClustersBuildDatas)
		//{
		//	for (auto& cluster : mesh.clustersCPU)
		//	{
		//		nums.push_back(cluster.numTriangles);
		//	}
		//}
		//std::sort(nums.begin(), nums.end());
		//for (auto num : nums)
		//	std::cout << num << " ";
	}


	// Create Vertex/Index Buffer (After Cluster created)
	{
		size_t vertexBufferSize = tempVerticesCPU.size() * vertexSize;
		size_t indexBufferSize = tempIndicesCPU.size() * sizeof(uint32_t);
		indices.count = static_cast<uint32_t>(tempIndicesCPU.size());
		vertices.count = static_cast<uint32_t>(tempVerticesCPU.size());
		//uint32_t numTriangles = indices.count / 3;

		//VkBufferUsageFlagBits additionalFlag = VkBufferUsageFlagBits(isSkinningModel ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0);


		device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vertexBufferSize, &vertices.buffer, &vertices.memory, transferQueue, vertexBufferByte.data());
		vertices.deviceAddress = getBufferDeviceAddress(vertices.buffer);
		vertices.size = vertexBufferSize;
		//myUtils::GPUDebug::Get()->setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vertices.buffer, "Model Vertex Buffer");
#if WATCH_AABB
		device->CreateBuffer_HostVisible(VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vertexBufferSize, &verticesHostVisible.buffer, &verticesHostVisible.memory, transferQueue, vertexBufferByte.data(), (void**)&vertexPositionViewer);
		
		/*device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vertexBufferSize, &vertices.buffer, &vertices.memory, transferQueue, vertexBufferByte.data());
		vertices.deviceAddress = getBufferDeviceAddress(vertices.buffer);

		device->CreateBuffer_HostVisible(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
			| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			,
			vertexBufferSize, &vertices.buffer, &vertices.memory, transferQueue, vertexBufferByte.data(), (void**)&vertexPositionViewer);*/
		//myUtils::GPUDebug::Get()->setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vertices.buffer, "Model Vertex Buffer");
#endif


		device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			indexBufferSize, &indices.buffer, &indices.memory, transferQueue, tempIndicesCPU.data());
		indices.size = indexBufferSize;

		if (isSkinningModel)
		{
			device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				vertexBufferSize, &deformingVertices.buffer, &deformingVertices.memory, transferQueue, vertexBufferByte.data());
			deformingVertices.size = vertexBufferSize;
			//myUtils::GPUDebug::Get()->setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)deformingVertices.buffer, "Model Deforming Vertex Buffer");

			deformingVertices.descriptor = { deformingVertices.buffer, 0, vertexBufferSize };
		}
	}

	// Process Raytracing Geometrynode per primitive or mesh
	uint32_t primitiveStartOffset = 0;
	uint32_t vertexStartOffset = 0;
	uint32_t indexStartOffset = 0;
	std::vector<MeshPrimitive> tempPrimitives; // for GeometryNodePerMesh, not for CLAS
	std::vector<ClusteredMeshPrimitive> tempClusteredPrimitives; // only for CLAS

	VkDeviceAddress vertexBaseDeviceAddress = 0;  
	VkDeviceAddress indexBaseDeviceAddress = getBufferDeviceAddress(indices.buffer);
	uint32_t meshIdx = 0u;
	uint32_t clusterStartOffset = 0u;
	for (auto& node : linearNodes)
	{
		if (node->mesh)
		{
			vertexBaseDeviceAddress = node->skin ? getBufferDeviceAddress(deformingVertices.buffer) : getBufferDeviceAddress(vertices.buffer);

			uint32_t vertexStartOffsetInMesh = 0u; // for Primitive
			uint32_t indexStartOffsetInMesh = 0u;
			if (isGeometryNodePerPrimitive)
			{
				for (const auto& primitive : node->mesh->primitives)
				{
					GeometryNodePerPrimitiveRT geometryNode{};
					VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
					VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
					vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(vertices.buffer);// bindless vertices
					indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(indices.buffer) + primitive->firstIndex * sizeof(uint32_t);
					geometryNode.vertexBufferDeviceAddress = vertexBufferDeviceAddress.deviceAddress;
					geometryNode.indexBufferDeviceAddress = indexBufferDeviceAddress.deviceAddress;
					geometryNode.textureIndexBaseColor = primitive->material.baseColorTexture->index;
					geometryNode.textureIndexOcclusion = primitive->material.occlusionTexture ? primitive->material.occlusionTexture->index : -1;
					geometryNodesPerPrimitive.push_back(geometryNode);
				}
			}
			else if (isGeometryNodePerMesh)
			{				
				GeometryNodePerMeshRT geometryNode{};
				geometryNode.vertexStartOffset = vertexStartOffset;
				geometryNode.indexStartOffset = indexStartOffset;
				geometryNode.primitiveStartOffset = primitiveStartOffset;

				for (const auto& primitive : node->mesh->primitives)
				{
					const Material& material = primitive->material;
					MeshPrimitive primitiveRT{};
					primitiveRT.textureIndexBaseColor = material.baseColorTexture ? static_cast<int32_t>(material.baseColorTexture->index) : -1;
					primitiveRT.textureIndexOcclusion = primitive->material.occlusionTexture ? material.occlusionTexture->index : -1;
					primitiveRT.vertexStartOffsetInMesh = vertexStartOffsetInMesh;
					primitiveRT.IndexStartOffsetInMesh = indexStartOffsetInMesh;
					tempPrimitives.push_back(primitiveRT); ++primitiveStartOffset;
					vertexStartOffset += primitive->vertexCount;
					indexStartOffset += primitive->indexCount;
					vertexStartOffsetInMesh += primitive->vertexCount;
					indexStartOffsetInMesh += primitive->indexCount;
				}
				geometryNodesPerMesh.push_back(geometryNode);
			}
			else if (bClusteredBLAS) // for Cluster Acceleration Structure
			{
				ClusteredGeometryNodeRT geometryNode{};
				geometryNode.vertexBufferDeviceAddress = vertexBaseDeviceAddress;
				geometryNode.indexBufferDeviceAddress = indexBaseDeviceAddress;
				geometryNode.triangleStartOffset = indexStartOffset / 3;
				uint32_t vertexCountInMesh = 0u;
				uint32_t IndexCountInMesh = 0u;
				for (const auto& primitive : node->mesh->primitives)
				{
					const Material& material = primitive->material;
					ClusteredMeshPrimitive primitiveRT{};
					primitiveRT.textureIndexBaseColor = material.baseColorTexture ? static_cast<int32_t>(material.baseColorTexture->index) : -1;
					primitiveRT.textureIndexOcclusion = material.occlusionTexture ? (int32_t)material.occlusionTexture->index : -1;
					primitiveRT.triangleStartOffsetGlobal = indexStartOffset / 3;
					//primitiveRT.vertexStartOffsetInMesh = vertexStartOffsetInMesh;
					//primitiveRT.IndexStartOffsetInMesh = indexStartOffsetInMesh;
					tempClusteredPrimitives.push_back(primitiveRT); ++primitiveStartOffset;
					vertexStartOffset += primitive->vertexCount;
					indexStartOffset += primitive->indexCount;
					vertexStartOffsetInMesh += primitive->vertexCount;
					indexStartOffsetInMesh += primitive->indexCount;
					vertexCountInMesh += primitive->vertexCount;
					IndexCountInMesh += primitive->indexCount;
				}
				geometryNode.geometryID = meshIdx;
				geometryNode.clusterStartOffset = clusterStartOffset;
				geometryNode.numClusters = perMeshClustersBuildDatas[meshIdx].clustersCPU.size();
				geometryNode.numVertices = vertexCountInMesh;
				geometryNode.numTriangles = (IndexCountInMesh / 3);
				clusteredGeometryNodes.push_back(geometryNode);
				clusterStartOffset += geometryNode.numClusters;
				++meshIdx;
			}
			//else if (bClusteredTriangleBLAS) // for cluster==gemoetry triangle blas
			//{
			//	
			//}
		}
	}

	getSceneDimensions();



	// find max frequency and max
	{
		for (uint32_t i = 0; i < clusterTriangleHistogram.size(); i++)
		{
			mostFrequentNumOfClusterTriangles = std::max(mostFrequentNumOfClusterTriangles, clusterTriangleHistogram[i]);
			if (clusterTriangleHistogram[i])
				m_clusterTriangleMax = i;
		}
		for (uint32_t i = 0; i < clusterVertexHistogram.size(); i++)
		{
			mostFrequentNumOfClusterVertices = std::max(mostFrequentNumOfClusterVertices, clusterVertexHistogram[i]);
			if (clusterVertexHistogram[i])
				m_clusterVertexMax = i;
		}
	}

	if (isBakedAnimation)
	{
		bakeAnimations();
	}

	// Device things
	// GeometryNode
	{
		size_t geometryNodeBufferSize = 0;
		void* geometryNodesData = nullptr;
		if (isGeometryNodePerPrimitive) {
			geometryNodeBufferSize = geometryNodesPerPrimitive.size() * sizeof(GeometryNodePerPrimitiveRT);
			geometryNodesData = static_cast<void*>(geometryNodesPerPrimitive.data());
		}
		else if (isGeometryNodePerMesh) {
			geometryNodeBufferSize = geometryNodesPerMesh.size() * sizeof(GeometryNodePerMeshRT);
			geometryNodesData = static_cast<void*>(geometryNodesPerMesh.data());
		}
		else if (bMakeClusters) {
			geometryNodeBufferSize = clusteredGeometryNodes.size() * sizeof(ClusteredGeometryNodeRT);
			geometryNodesData = static_cast<void*>(clusteredGeometryNodes.data());
		}

#if WATCH_GEOMETRYNODE
		device->CreateBuffer_HostVisible(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			geometryNodeBufferSize, &geometryNodes.buffer, &geometryNodes.memory, transferQueue, geometryNodesData, (void**)&geometryNodeViewer);
#else
		device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			geometryNodeBufferSize, &geometryNodes.buffer, &geometryNodes.memory, transferQueue, geometryNodesData);
#endif
		geometryNodes.size = geometryNodeBufferSize;

		// Create Descriptor Info for Raytracing
		{
			geometryNodes.deviceAddress = getBufferDeviceAddress(geometryNodes.buffer);
			// Create Descriptor
			geometryNodes.descriptor = { geometryNodes.buffer, 0, geometryNodeBufferSize };
		}		
	}
	// For Primitives
	if (isGeometryNodePerMesh)
	{
		size_t primitiveBufferSize = tempPrimitives.size() * sizeof(MeshPrimitive);

		device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			primitiveBufferSize, &primitives.buffer, &primitives.memory, transferQueue, tempPrimitives.data());

		primitives.descriptor = { primitives.buffer, 0, primitiveBufferSize };
	}
	else if (bMakeClusters) // For cluster
	{
		size_t primitiveBufferSize = tempClusteredPrimitives.size() * sizeof(ClusteredMeshPrimitive);

		device->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			primitiveBufferSize, &primitives.buffer, &primitives.memory, transferQueue, tempClusteredPrimitives.data());
		primitives.count = tempClusteredPrimitives.size();
		primitives.size = primitiveBufferSize;
		primitives.descriptor = { primitives.buffer, 0, primitiveBufferSize };

		assert(clusters.count > 0);
		size_t clusterBufferSize = clusters.count * sizeof(ClusterRT);
		device->CreateBuffer_HostVisible(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			clusterBufferSize, &clusters.buffer, &clusters.memory, transferQueue, tempClusters.data(), (void**)&clusterViewer);
		clusters.descriptor = { clusters.buffer, 0, clusterBufferSize };
	}
	if (bCombinedMeshBuffer)
	{
		VkDeviceSize bufferSize = linearMeshes.size() * sizeof(Mesh::UniformBlock);
		device->CreateBuffer_HostVisible(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			bufferSize, &combinedMeshBuffer.buffer, &combinedMeshBuffer.memory, true, nullptr, &combinedMeshBuffer.mapped);
		combinedMeshBuffer.descriptor = { combinedMeshBuffer.buffer, 0, bufferSize };

		updateCombinedMeshBuffer();
	}
	// Setup descriptors
	uint32_t uboCount{ 0 };
	uint32_t imageCount{ 0 };
	// Case : each mesh has its descriptor
	const bool hasMultipleUbo = (preTransform == false) || bCombinedMeshBuffer == false;
	if (hasMultipleUbo)
	{
		if (isBakedAnimation)
		{
			uboCount = bakedAnimations.size();
		}
		else
		{
			for (auto& node : linearNodes) {
				if (node->mesh) {
					uboCount++;
				}
			}			
		}
	}
	else uboCount = 1;

	for (auto& material : materials) {
		if (material.baseColorTexture != nullptr) {
			imageCount++;
		}
	}
	std::vector<VkDescriptorPoolSize> poolSizes;
	VkDescriptorType modelDescriptorType = bCombinedMeshBuffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes.push_back(VkDescriptorPoolSize{ modelDescriptorType, uboCount });
		if (imageCount > 0) {
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
		}
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
		}
	}
	VkDescriptorPoolCreateInfo descriptorPoolCI{};
	descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolCI.pPoolSizes = poolSizes.data();
	descriptorPoolCI.maxSets = uboCount + imageCount * 2*2;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));

	// Descriptors for per-node uniform buffers
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutModel == VK_NULL_HANDLE) {
			uint32_t additionalFlag = isSkinningModel ? VK_SHADER_STAGE_COMPUTE_BIT : 0;
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				// [model matrix] or [modelMat + Skinning info]
				vks::initializers::descriptorSetLayoutBinding(modelDescriptorType, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT | additionalFlag, modelBufferBinding),
			};
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutModel));
		}

		if (preTransform && !isSkinningModel)
		{
			// Create bufffer
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				sizeof(UniformData),
				&representingBuffer.buffer,
				&representingBuffer.memory,
				&uniformBlock));
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, representingBuffer.memory, 0, sizeof(UniformData), 0, &representingBuffer.mapped));
			representingBuffer.descriptor = { representingBuffer.buffer, 0, sizeof(UniformData) };

			// allocate descriptor
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayoutModel;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &representingBuffer.descriptorSet));

			// update
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = representingBuffer.descriptorSet;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pBufferInfo = &representingBuffer.descriptor;

			vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
		}
		else // prepare all meshes' ubo
		{
			if (isBakedAnimation)
			{
				uint32_t numAllBakedAnimations = bakedAnimations.size();
				bakedUniformBuffers.resize(numAllBakedAnimations);
				for (uint32_t i = 0; i < numAllBakedAnimations; ++i)
				{
					auto& bakedUniformBuffer = bakedUniformBuffers[i];
					VkDeviceSize bufferSize = sizeof(BakedAnimation);
					// Create bufffer
					device->CreateBuffer_DeviceLocal(
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						bufferSize,
						&bakedUniformBuffer.buffer,
						&bakedUniformBuffer.memory,
						transferQueue,
						&bakedAnimations[i]);
					bakedUniformBuffer.descriptor = { bakedUniformBuffer.buffer, 0, bufferSize };

					// allocate descriptor
					VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
					descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
					descriptorSetAllocInfo.descriptorPool = descriptorPool;
					descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayoutModel;
					descriptorSetAllocInfo.descriptorSetCount = 1;
					VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &bakedUniformBuffer.descriptorSet));

					// update
					VkWriteDescriptorSet writeDescriptorSet{};
					writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					writeDescriptorSet.descriptorCount = 1;
					writeDescriptorSet.dstSet = bakedUniformBuffer.descriptorSet;
					writeDescriptorSet.dstBinding = modelBufferBinding;
					writeDescriptorSet.pBufferInfo = &bakedUniformBuffer.descriptor;

					vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
				}
			}
			else if (bCombinedMeshBuffer)
			{
				// allocate descriptor
				VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
				descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				descriptorSetAllocInfo.descriptorPool = descriptorPool;
				descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayoutModel;
				descriptorSetAllocInfo.descriptorSetCount = 1;
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &combinedMeshBuffer.descriptorSet));

				// update
				VkWriteDescriptorSet writeDescriptorSet{};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writeDescriptorSet.descriptorCount = 1;
				writeDescriptorSet.dstSet = combinedMeshBuffer.descriptorSet;
				writeDescriptorSet.dstBinding = 0;
				writeDescriptorSet.pBufferInfo = &combinedMeshBuffer.descriptor;

				vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
			}
			else
			{
				for (auto node : nodes) {
					prepareNodeDescriptor(node, descriptorSetLayoutModel);
				}
			}
		}
	}


	// Descriptors for per-material images
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
			setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
			setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
			/*if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			}
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			}*/
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
		}
		for (auto& material : materials) {
			if (material.baseColorTexture != nullptr) {
				material.createDescriptorSet(descriptorPool, descriptorSetLayoutImage, descriptorBindingFlags);
			}
		}
	}

	// release
	for (auto& vertex : tempVerticesCPU)
	{
		delete vertex; vertex = nullptr;
	}

}

void myglTF::ModelRT::bindBuffers(VkCommandBuffer commandBuffer)
{
	const VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	buffersBound = true;
}

void myglTF::ModelRT::getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f) * node->getMatrix();
			glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f) * node->getMatrix();
			if (locMin.x < min.x) { min.x = locMin.x; }
			if (locMin.y < min.y) { min.y = locMin.y; }
			if (locMin.z < min.z) { min.z = locMin.z; }
			if (locMax.x > max.x) { max.x = locMax.x; }
			if (locMax.y > max.y) { max.y = locMax.y; }
			if (locMax.z > max.z) { max.z = locMax.z; }
		}
	}
	for (auto child : node->children) {
		getNodeDimensions(child, min, max);
	}
}

void myglTF::ModelRT::getSceneDimensions()
{
	dimensions.min = glm::vec3(FLT_MAX);
	dimensions.max = glm::vec3(-FLT_MAX);
	for (auto node : nodes) {
		getNodeDimensions(node, dimensions.min, dimensions.max);
	}
	dimensions.size = dimensions.max - dimensions.min;
	dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
	dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void myglTF::ModelRT::updateAnimation(uint32_t index, float time)
{
	if (index > static_cast<uint32_t>(animations.size()) - 1) {
		std::cout << "No animation with index " << index << std::endl;
		return;
	}
	Animation& animation = animations[index];

	bool updated = false;
	for (auto& channel : animation.channels) {
		myglTF::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
		if (sampler.inputs.size() > sampler.outputsVec4.size()) {
			continue;
		}

		for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
			if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
				float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
				if (u <= 1.0f) {
					switch (channel.path) {
					case myglTF::AnimationChannel::PathType::TRANSLATION: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->translation = glm::vec3(trans);
						break;
					}
					case myglTF::AnimationChannel::PathType::SCALE: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->scale = glm::vec3(trans);
						break;
					}
					case myglTF::AnimationChannel::PathType::ROTATION: {
						glm::quat q1;
						q1.x = sampler.outputsVec4[i].x;
						q1.y = sampler.outputsVec4[i].y;
						q1.z = sampler.outputsVec4[i].z;
						q1.w = sampler.outputsVec4[i].w;
						glm::quat q2;
						q2.x = sampler.outputsVec4[i + 1].x;
						q2.y = sampler.outputsVec4[i + 1].y;
						q2.z = sampler.outputsVec4[i + 1].z;
						q2.w = sampler.outputsVec4[i + 1].w;
						channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
						break;
					}
					}
					updated = true;
				}
			}
		}
	}
}

void myglTF::ModelRT::updateNodeTransforms()
{
	for (auto& node : nodes)
		node->update();
}

void myglTF::ModelRT::updateJoints()
{
	for (auto& rootToMatrices : rootToMatricesMap)
	{
		Node* jointRoot = rootToMatrices.first;
		std::array<glm::mat4, 256>& jointMatrices = rootToMatrices.second;
		jointRoot->updateJoints(glm::mat4(1.f), jointMatrices);
	}
}

void myglTF::ModelRT::updateNodeTransforms(Node* pNode)
{
	if (pNode->mesh) {
		glm::mat4 m = pNode->getMatrix();
		if (pNode->skin) {
			pNode->mesh->uniformBlock.matrix = m;

			const std::array<glm::mat4, MAX_JOINTS>& jointMatrices = rootToMatricesMap[pNode->skin->jointRoot];

			//glm::mat4 inverseTransform = glm::inverse(m); // inverse(node to world)
			for (size_t i = 0; i < pNode->skin->joints.size(); i++) {
				myglTF::Node* jointNode = pNode->skin->joints[i];
				// No need to Multiply inverse of m(nodeWorld or MeshWorld), because jointMatrices is already MESH LOCAL
				glm::mat4 jointMat = jointMatrices[i] * pNode->skin->inverseBindMatrices[i];
				pNode->mesh->uniformBlock.jointMatrix[i] = jointMat;
			}
			//pNode->mesh->uniformBlock.jointcount = (float)pNode->skin->joints.size();
			memcpy(pNode->mesh->uniformBuffer.mapped, &pNode->mesh->uniformBlock, sizeof(pNode->mesh->uniformBlock));

		}
		else {
			memcpy(pNode->mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
		}
	}

	for (auto& child : pNode->children) {
		updateNodeTransforms(child);
	}
}
void myglTF::ModelRT::updateCombinedMeshBuffer()
{
	constexpr uint64_t stride = sizeof(Mesh::UniformBlock);
	for (uint32_t i = 0; i < linearMeshes.size(); ++i)
	{
		memcpy((uint8_t*)combinedMeshBuffer.mapped + stride * i, &linearMeshes[i]->uniformBlock, stride);
	}
}

myglTF::Node* myglTF::ModelRT::findNode(Node* parent, uint32_t index)
{
	Node* nodeFound = nullptr;
	if (parent->index == index) {
		return parent;
	}
	for (auto& child : parent->children) {
		nodeFound = findNode(child, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

myglTF::Node* myglTF::ModelRT::nodeFromIndex(uint32_t index)
{
	Node* nodeFound = nullptr;
	for (auto& node : nodes) {
		nodeFound = findNode(node, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

void myglTF::ModelRT::prepareNodeDescriptor(myglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout)
{
	if (node->mesh) {
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
		writeDescriptorSet.dstBinding = modelBufferBinding;
		writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

		vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
	for (auto& child : node->children) {
		prepareNodeDescriptor(child, descriptorSetLayout);
	}
}
myglTF::Texture* myglTF::ModelRT::getTexture(uint32_t index)
{
	if (index < textures.size()) {
		return &textures[index];
	}
	return nullptr;
}

void myglTF::ModelRT::createEmptyTexture(VkQueue transferQueue)
{
	emptyTexture.device = device;
	emptyTexture.width = 1;
	emptyTexture.height = 1;
	emptyTexture.layerCount = 1;
	emptyTexture.mipLevels = 1;

	size_t bufferSize = emptyTexture.width * emptyTexture.height * 4;
	unsigned char* buffer = new unsigned char[bufferSize];
	memset(buffer, 0, bufferSize);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
	bufferCreateInfo.size = bufferSize;
	// This buffer is used as a transfer source for the buffer copy
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
	VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

	// Copy texture data into staging buffer
	uint8_t* data{ nullptr };
	VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
	memcpy(data, buffer, bufferSize);
	vkUnmapMemory(device->logicalDevice, stagingMemory);

	VkBufferImageCopy bufferCopyRegion = {};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = emptyTexture.width;
	bufferCopyRegion.imageExtent.height = emptyTexture.height;
	bufferCopyRegion.imageExtent.depth = 1;

	// Create optimal tiled target image
	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.extent = { emptyTexture.width, emptyTexture.height, 1 };
	imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &emptyTexture.image));

	vkGetImageMemoryRequirements(device->logicalDevice, emptyTexture.image, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &emptyTexture.deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, emptyTexture.image, emptyTexture.deviceMemory, 0));

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;

	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	vks::tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
	vks::tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
	device->flushCommandBuffer(copyCmd, transferQueue);
	emptyTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Clean up staging resources
	vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

	VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerCreateInfo.maxAnisotropy = 1.0f;
	VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &emptyTexture.sampler));

	VkImageViewCreateInfo viewCreateInfo = vks::initializers::imageViewCreateInfo();
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	viewCreateInfo.subresourceRange.levelCount = 1;
	viewCreateInfo.image = emptyTexture.image;
	VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &emptyTexture.view));

	emptyTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	emptyTexture.descriptor.imageView = emptyTexture.view;
	emptyTexture.descriptor.sampler = emptyTexture.sampler;
}

void myglTF::ModelRT::bakeAnimations()
{
	myUtils::ScopedCPUTimer scopedTimer = myUtils::ScopedCPUTimer("bakeAnim Timer");
	animMaxFrame = 0.f;
	samplingRate = FLT_MAX;
	for (auto& anim : activeAnimations)
	{
		for (auto& channel : anim.channels) {
			myglTF::AnimationSampler& sampler = anim.samplers[channel.samplerIndex];
			if (sampler.inputs.size() > sampler.outputsVec4.size())
				continue;

			for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
				samplingRate = std::min(samplingRate, sampler.inputs[i + 1] - sampler.inputs[i]);
			}
		}
	}
	animMaxFPS = (uint32_t)(1.f / samplingRate);
	for (auto& anim : activeAnimations)
		animMaxFrame = std::max(animMaxFrame, anim.end); // still max time yet
	animMaxTime = animMaxFrame;
	animMaxFrame *= (1.f / samplingRate); // maxTime * FPS
	bakedAnimations.reserve((uint32_t)animMaxFrame);


	for (uint32_t frame = 0; frame < (uint32_t)animMaxFrame; ++frame)
	{
		for (uint32_t animIdx = 0; animIdx < activeAnimations.size(); ++animIdx) // animTypes
		{
			Animation& animation = animations[animIdx];
			float time = (float)frame * samplingRate + 0.001f; // second

			bool updated = false;
			for (auto& channel : animation.channels) {
				myglTF::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
				if (sampler.inputs.size() > sampler.outputsVec4.size()) {
					continue;
				}

				for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
					if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
						float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
						if (u <= 1.0f) {
							switch (channel.path) {
							case myglTF::AnimationChannel::PathType::TRANSLATION: {
								glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
								channel.node->translation = glm::vec3(trans);
								break;
							}
							case myglTF::AnimationChannel::PathType::SCALE: {
								glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
								channel.node->scale = glm::vec3(trans);
								break;
							}
							case myglTF::AnimationChannel::PathType::ROTATION: {
								glm::quat q1;
								q1.x = sampler.outputsVec4[i].x;
								q1.y = sampler.outputsVec4[i].y;
								q1.z = sampler.outputsVec4[i].z;
								q1.w = sampler.outputsVec4[i].w;
								glm::quat q2;
								q2.x = sampler.outputsVec4[i + 1].x;
								q2.y = sampler.outputsVec4[i + 1].y;
								q2.z = sampler.outputsVec4[i + 1].z;
								q2.w = sampler.outputsVec4[i + 1].w;
								channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
								break;
							}
							}
							updated = true;
						}
					}
				}
			}
			if (updated) // this animation has updated pose at currentTime
			{
				for (auto& node : nodes) {
					node->update();
				}
			}
		}
		// after all anims cur time finished
		for (const auto& mesh : linearMeshes)
		{
			BakedAnimation bakedAnimData{};
			memcpy(bakedAnimData.jointMats, mesh->uniformBlock.jointMatrix, sizeof(BakedAnimation::jointMats));
			bakedAnimations.push_back(bakedAnimData);
		}
	}
}