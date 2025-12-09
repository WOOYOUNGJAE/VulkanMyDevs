#include "myMeshClustrizingMeshopt.h"

#include "myIncludesCPUGPU.h"
#include "myUtils.h"
#include "myCudaInteropt.h"
#include <set>

#include "mySimpleAnimComputePass.h"

#include "meshoptimizer.h"

#define SCENE_LOCAL_PATH(NAME) "D:\\Documents\\Blender\\Exports\\Scene\\" NAME ".gltf"



#define BLAS_PER_CLUSTER 0
#define GEOMETRY_PER_CLUSTER !BLAS_PER_CLUSTER
#define BINDLESS_SKINNING 0
static int fixedBlasNum;

uint32_t numBLASesMax;
MyMeshClustrizingMeshopt::MyMeshClustrizingMeshopt()
{
	title = "MyMeshClustrizingMeshopt (" + std::to_string(width) + "x" + std::to_string(height) + ")";
	camera.type = Camera::CameraType::firstperson;
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
	camera.setRotation(glm::vec3(-1.0f, 0.f, 0.0f));
	camera.setTranslation(glm::vec3(0.0f, -0.020649, -1.168693));
	camera.setTranslation(glm::vec3(0.0f, 0.035266, -1.168693));

	enableExtensions();

	// Buffer device address requires the 64-bit integer feature to be enabled
	enabledFeatures.shaderInt64 = VK_TRUE;

	// for pause action
	//camera.setTranslation(glm::vec3(-0.832603, 0.060284, 0.074787));
	//camera.setRotation(glm::vec3(-0.000000, -90.000000, 0.000000));
}

MyMeshClustrizingMeshopt::~MyMeshClustrizingMeshopt()
{
	if (device) {
		// release compute pipeline
		{
			vkDestroyPipeline(device, computePipeline, nullptr);
			vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
		}
		// delete scratches
		{
			deleteScratchBuffer(tlasScratchBuffer);
			for (auto& blasBuildInfo : staticPerBlasBuildInfos)
			{
				deleteScratchBuffer(blasBuildInfo.blasScratchBuffer);
			}
			for (auto& blasBuildInfo : dynamicPerBlasBuildInfos)
			{
				deleteScratchBuffer(blasBuildInfo.blasScratchBuffer);
			}
		}
		// delete Buffers for building ASes
		{
			vkDestroyBuffer(vulkanDevice->logicalDevice, blasInstancesBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, blasInstancesBuffer.memory, nullptr);
		}


		vkDestroyPipeline(device, rtPipeline, nullptr);
		vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, rtDescriptorSetLayout, nullptr);
		deleteStorageImage();
		// Delete Acceleration Structures
		{
			for (auto& blas : staticBLASes)
				deleteAccelerationStructure(blas);
			for (auto& blas : dynamicBLASes)
				deleteAccelerationStructure(blas);
			deleteAccelerationStructure(TLAS);
		}
		vertexBuffer.destroy();
		indexBuffer.destroy();
		transformBuffer.destroy();
		shaderBindingTables.raygen.destroy();
		shaderBindingTables.miss.destroy();
		shaderBindingTables.hit.destroy();
		uniformBuffer.destroy();
	}
}

void MyMeshClustrizingMeshopt::createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure,
	VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo, VkBufferUsageFlagBits usageFlag)
{
	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
	bufferCreateInfo.usage = usageFlag | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &accelerationStructure.buffer));
	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(device, accelerationStructure.buffer, &memoryRequirements);
	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	VkMemoryAllocateInfo memoryAllocateInfo{};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &accelerationStructure.memory));
	VK_CHECK_RESULT(vkBindBufferMemory(device, accelerationStructure.buffer, accelerationStructure.memory, 0));
}

void MyMeshClustrizingMeshopt::initBlases()
{
	VkTransformMatrixKHR transformMatrix = {
   1.0f, 0.0f, 0.0f, 0.0f,
   0.0f, 1.0f, 0.0f, 0.0f,
   0.0f, 0.0f, 1.0f, 0.0f };

	// Transform buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&transformBuffer,
		sizeof(VkTransformMatrixKHR),
		&transformMatrix));


	// clustered blases are always deformable object
	{
		// per cluster
		for (uint32_t i = 0; i < clusters.size(); ++i)
		{
			// empalce back and get reference - for avoiding dangling pointer due to moving array;
			PerBLASBuildInfo& refPerBlasBuildInfo = dynamicPerBlasBuildInfos.emplace_back(PerBLASBuildInfo{});
			std::vector<uint32_t> maxPrimitiveCounts{};
			const MyMeshClustrizingMeshopt::ClusterRT& cluster = clusters[i];

			// Build
			// One geometry per glTF node, so we can index materials using gl_GeometryIndexEXT

			VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
			VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
			VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

			vertexBufferDeviceAddress.deviceAddress = model.m_deformingVertexBuffer.deviceAddress; // all scene vertices
			indexBufferDeviceAddress.deviceAddress = model.m_indexBuffer.deviceAddress + (cluster.firstTriangle * 3) * sizeof(uint32_t);
			transformBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(transformBuffer.buffer);

			// gl_GeometryIndexEXT
			VkAccelerationStructureGeometryKHR asGeometry{}; // geometry == cluseter
			asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			asGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
			asGeometry.geometry.triangles.maxVertex = cluster.gmcCluster.vertexCount + 1;
			asGeometry.geometry.triangles.vertexStride = sizeof(glm::vec3);
			asGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
			asGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
			asGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;
			refPerBlasBuildInfo.asGeometries.push_back(asGeometry);

			maxPrimitiveCounts.push_back(cluster.gmcCluster.triangleCount);

			VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
			buildRangeInfo.firstVertex = 0;
			buildRangeInfo.primitiveOffset = 0; // primitive->firstIndex * sizeof(uint32_t);
			buildRangeInfo.primitiveCount = cluster.gmcCluster.triangleCount;
			buildRangeInfo.transformOffset = 0;
			refPerBlasBuildInfo.buildRangeInfos.push_back(buildRangeInfo);

			// Get size info
			VkAccelerationStructureBuildGeometryInfoKHR& accelerationStructureBuildGeometryInfo = refPerBlasBuildInfo.asBuildGeometryInfo;
			accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

			//accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
			accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
			//accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

			accelerationStructureBuildGeometryInfo.geometryCount = static_cast<uint32_t>(refPerBlasBuildInfo.asGeometries.size());
			accelerationStructureBuildGeometryInfo.pGeometries = refPerBlasBuildInfo.asGeometries.data();

			VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
			accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
			vkGetAccelerationStructureBuildSizesKHR(
				device,
				VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&accelerationStructureBuildGeometryInfo,
				maxPrimitiveCounts.data(),
				&accelerationStructureBuildSizesInfo);
			refPerBlasBuildInfo.asSize = accelerationStructureBuildSizesInfo.accelerationStructureSize;

			AccelerationStructure blas{};
			MyVulkanRTBase::createAccelerationStructureBuffer(blas, accelerationStructureBuildSizesInfo);
			refPerBlasBuildInfo.blasScratchSizeMax = std::max(accelerationStructureBuildSizesInfo.buildScratchSize, accelerationStructureBuildSizesInfo.updateScratchSize);
			dynamicBLASes.push_back(blas);
		}
	}
	auto updateDeviceAddresses = [&](auto& blasArray)
		{
			for (auto& blas : blasArray)
			{
				blas.deviceAddress = getBufferDeviceAddress(blas.buffer);
			}
		};
	updateDeviceAddresses(staticBLASes);
	updateDeviceAddresses(dynamicBLASes);
}

void MyMeshClustrizingMeshopt::initTLAS()
{
	VkTransformMatrixKHR transformMatrix = {
		   1.0f, 0.0f, 0.0f, 0.0f,
		   0.0f, 0.0f, 1.0f, 0.0f,
		   0.0f, -1.0f, 0.0f, 0.0f,
	};


	for (auto& blas : dynamicBLASes)
	{
		VkAccelerationStructureInstanceKHR blasInstance{};
		blasInstance.transform = transformMatrix;
		blasInstance.instanceCustomIndex = 0xff; // dynamicBlasCustomIndex
		blasInstance.mask = 0xFF;
		blasInstance.instanceShaderBindingTableRecordOffset = 0;
		blasInstance.flags = 0;
		blasInstance.accelerationStructureReference = blas.deviceAddress;
		blasInstances.push_back(blasInstance);
	}
	blasInstances.shrink_to_fit();

	uint32_t numBlasInstances = blasInstances.size();

	// Buffer for instance data
	vulkanDevice->CreateBuffer_DeviceLocal(
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		sizeof(VkAccelerationStructureInstanceKHR) * numBlasInstances,
		&blasInstancesBuffer.buffer, &blasInstancesBuffer.memory, graphicsQueue,
		blasInstances.data());
	blasInstancesBuffer.deviceAddress = getBufferDeviceAddress(blasInstancesBuffer.buffer);

	VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
	instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(blasInstancesBuffer.buffer);

	tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	tlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
	tlasGeometry.geometry.instances.data = instanceDataDeviceAddress;

	// Get size info
	/*
		The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
	*/
	tlasBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	tlasBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
#if FORCE_STATIC_SCENE
	tlasBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
#else
	tlasBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
#endif
	tlasBuildGeometryInfo.geometryCount = 1; // num TLAS
	tlasBuildGeometryInfo.pGeometries = &tlasGeometry;


	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&tlasBuildGeometryInfo,
		&numBlasInstances,
		&accelerationStructureBuildSizesInfo);
	tlasScratchSize = std::max(accelerationStructureBuildSizesInfo.buildScratchSize, accelerationStructureBuildSizesInfo.updateScratchSize);
	tlasSize = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	MyVulkanRTBase::createAccelerationStructureBuffer(TLAS, accelerationStructureBuildSizesInfo);

	// Create a small scratch buffer used during build of the top level acceleration structure
	tlasScratchBuffer = createScratchBuffer(tlasScratchSize);
}

void MyMeshClustrizingMeshopt::buildBLASes(VkCommandBuffer cmdBuffer)
{
	uint32_t numStaticBlases = staticBLASes.size();
	uint32_t numDynamicBlases = dynamicBLASes.size(); // for Deformable Mesh

	const bool isFirstBuild = (numStaticBlases && staticBLASes[0].handle == VK_NULL_HANDLE)
		|| (numDynamicBlases && dynamicBLASes[0].handle == VK_NULL_HANDLE);

	// ramda func
	auto processBLASes = [&](auto& blases, auto& buildInfos, auto& buildingSets) {
		for (uint32_t blasIdx = 0; blasIdx < buildInfos.size(); ++blasIdx) {
			AccelerationStructure& blas = blases[blasIdx];
			PerBLASBuildInfo& refBuildInfo = buildInfos[blasIdx];

			if (isFirstBuild)
			{
				if (refBuildInfo.blasScratchBuffer.handle == VK_NULL_HANDLE)
					refBuildInfo.blasScratchBuffer = createScratchBuffer(refBuildInfo.blasScratchSizeMax);

				VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
				accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
				accelerationStructureCreateInfo.buffer = blas.buffer;
				accelerationStructureCreateInfo.size = refBuildInfo.asSize;
				accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &blas.handle);

				refBuildInfo.asBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				refBuildInfo.asBuildGeometryInfo.dstAccelerationStructure = blas.handle;
				refBuildInfo.asBuildGeometryInfo.scratchData.deviceAddress = refBuildInfo.blasScratchBuffer.deviceAddress;

				buildingSets.buildRangeInfosArray.push_back(refBuildInfo.buildRangeInfos.data());
				buildingSets.buildGeometryInfos.push_back(refBuildInfo.asBuildGeometryInfo);
			}
			else // Update
			{
#if FORCE_STATIC_SCENE
				return;
#endif
				refBuildInfo.asBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
				refBuildInfo.asBuildGeometryInfo.srcAccelerationStructure = blas.handle;
				refBuildInfo.asBuildGeometryInfo.dstAccelerationStructure = blas.handle;
			}
		}
		};

	// static blas
	processBLASes(staticBLASes, staticPerBlasBuildInfos, staticBlasBuildingSets);
	// dynamic blas  
	processBLASes(dynamicBLASes, dynamicPerBlasBuildInfos, dynamicBlasBuildingSets);


	// dynamic blas
	if (numDynamicBlases)
	{
		if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
		vkCmdBuildAccelerationStructuresKHR(
			cmdBuffer,
			numDynamicBlases,
			dynamicBlasBuildingSets.buildGeometryInfos.data(),
			dynamicBlasBuildingSets.buildRangeInfosArray.data());
		if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
	}

	// static blas
	if (numStaticBlases)
	{
		vkCmdBuildAccelerationStructuresKHR(
			cmdBuffer,
			numStaticBlases,
			staticBlasBuildingSets.buildGeometryInfos.data(),
			staticBlasBuildingSets.buildRangeInfosArray.data());
	}

}

void MyMeshClustrizingMeshopt::buildTLAS(VkCommandBuffer cmdBuffer)
{
	const bool isFirstBuild = (TLAS.handle == VK_NULL_HANDLE);
	//VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	if (isFirstBuild)
	{
		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = TLAS.buffer;
		accelerationStructureCreateInfo.size = tlasSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &TLAS.handle);


		tlasBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		tlasBuildGeometryInfo.dstAccelerationStructure = TLAS.handle;
		tlasBuildGeometryInfo.pGeometries = &tlasGeometry;
		tlasBuildGeometryInfo.scratchData.deviceAddress = tlasScratchBuffer.deviceAddress;
	}
	else // !isFirstBuild
	{
#if FORCE_STATIC_SCENE
		return;
#endif
		tlasBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
		tlasBuildGeometryInfo.srcAccelerationStructure = TLAS.handle;
		tlasBuildGeometryInfo.dstAccelerationStructure = TLAS.handle;
	}

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = blasInstances.size();
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

	// Build the acceleration structure on the device via a one-time command buffer submission
	// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds

	if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
	vkCmdBuildAccelerationStructuresKHR(
		cmdBuffer,
		1,
		&tlasBuildGeometryInfo,
		accelerationBuildStructureRangeInfos.data());
	if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);


	// after first build complete
	if (isFirstBuild)
	{
		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = TLAS.handle;
		TLAS.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationDeviceAddressInfo);
	}
}

void MyMeshClustrizingMeshopt::createShaderBindingTables()
{
	const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
	const uint32_t handleSizeAligned = vks::tools::alignedSize(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupHandleAlignment);
	const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
	const uint32_t sbtSize = groupCount * handleSizeAligned;

	std::vector<uint8_t> shaderHandleStorage(sbtSize);
	VK_CHECK_RESULT(vkGetRayTracingShaderGroupHandlesKHR(device, rtPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

	createShaderBindingTable(shaderBindingTables.raygen, 1);
	createShaderBindingTable(shaderBindingTables.miss, 2);
	createShaderBindingTable(shaderBindingTables.hit, 1);

	// Copy handles
	memcpy(shaderBindingTables.raygen.mapped, shaderHandleStorage.data(), handleSize);
	// We are using two miss shaders, so we need to get two handles for the miss shader binding table
	memcpy(shaderBindingTables.miss.mapped, shaderHandleStorage.data() + handleSizeAligned, handleSize * 2);
	memcpy(shaderBindingTables.hit.mapped, shaderHandleStorage.data() + handleSizeAligned * 3, handleSize);
}

void MyMeshClustrizingMeshopt::createComputePipeline()
{
	animComputePass = model.m_isSkeletalMesh ? std::make_unique<MyTwistComputePass >(device) : nullptr;

	// Force make
	animComputePass = std::make_unique<MyTwistComputePass >(device);

	if (animComputePass)
	{
		animComputePass->createDescriptorSets(model, model.m_deformingVertexBuffer);
		animComputePass->createPipeline(getShadersPath() + "myMeshClusteringCuda/animTwist.comp.spv");
	}
}

void MyMeshClustrizingMeshopt::createRayTracingPipeline()
{
	const uint32_t imageCount = 0;

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		// Binding 0: Top level acceleration structure
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
		// Binding 1: Ray tracing result image
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1),
		// Binding 2: Uniform buffer
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 2),
	};


	// Unbound set
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT setLayoutBindingFlags{};
	setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	setLayoutBindingFlags.bindingCount = setLayoutBindings.size();
	std::vector<VkDescriptorBindingFlagsEXT> descriptorBindingFlags = {
		0,
		0,
		0,
	};
	setLayoutBindingFlags.pBindingFlags = descriptorBindingFlags.data();

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	descriptorSetLayoutCI.pNext = &setLayoutBindingFlags;
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &rtDescriptorSetLayout));

	// Push constant - bind scene vertex/index buffer device address
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstantData);

	VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&rtDescriptorSetLayout, 1);
	pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &rtPipelineLayout));

	/*
		Setup ray tracing shader groups
	*/
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	VkSpecializationMapEntry* specializationEntries = nullptr;
	// Ray generation group
	{
		shaderStages.push_back(loadShader(getShadersPath() + "myMeshClusteringCuda/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		shaderGroups.push_back(shaderGroup);
	}

	// Miss group
	{
		shaderStages.push_back(loadShader(getShadersPath() + "myMeshClusteringCuda/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		shaderGroups.push_back(shaderGroup);
		// Second shader for shadows
		shaderStages.push_back(loadShader(getShadersPath() + "myMeshClusteringCuda/shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}

	// Closest hit group for doing texture lookups
	{
		VkPipelineShaderStageCreateInfo& shaderStage = shaderStages.emplace_back(loadShader(getShadersPath() + "myMeshClusteringCuda/closesthit_CLUSTER_BLAS1.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));

		uint32_t numEntries = sizeof(SpecialzationData) / sizeof(float);
		specializationEntries = new VkSpecializationMapEntry[numEntries]{};
		for (uint32_t i = 0; i < numEntries; ++i)
		{
			specializationEntries[i].size = sizeof(float);
			specializationEntries[i].constantID = i;
			specializationEntries[i].offset = sizeof(float) * i;
		}
		VkSpecializationInfo specializationInfo
		{
			.mapEntryCount = numEntries,
			.pMapEntries = specializationEntries,
			.dataSize = sizeof(SpecialzationData),
			.pData = &specializationData
		};
		shaderStage.pSpecializationInfo = &specializationInfo;

		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		// This group also uses an anyhit shader for doing transparency (see anyhit.rahit for details)
		shaderStages.push_back(loadShader(getShadersPath() + "myMeshClusteringCuda/anyhit.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
		shaderGroup.anyHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}


	/*
		Create the ray tracing pipeline
	*/
	VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
	rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	rayTracingPipelineCI.pStages = shaderStages.data();
	rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
	rayTracingPipelineCI.pGroups = shaderGroups.data();
	rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
	rayTracingPipelineCI.layout = rtPipelineLayout;

	VK_CHECK_RESULT(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &rtPipeline));

	delete[] specializationEntries; specializationEntries = nullptr;
}

void MyMeshClustrizingMeshopt::createDescriptorSets()
{
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }, // Binding 0: Top level acceleration structure
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, // Binding 1: Ray tracing result image
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, // Binding 2: Uniform data
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // Binding 4: Geometry node information SSBO
	};

	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountAllocInfo{};
	uint32_t variableDescCounts[] = { 0 };
	variableDescriptorCountAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
	variableDescriptorCountAllocInfo.descriptorSetCount = 1;
	variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &rtDescriptorSetLayout, 1);
	descriptorSetAllocateInfo.pNext = &variableDescriptorCountAllocInfo;
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &rtDescriptorSet));

	VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = vks::initializers::writeDescriptorSetAccelerationStructureKHR();
	descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
	descriptorAccelerationStructureInfo.pAccelerationStructures = &TLAS.handle;

	VkWriteDescriptorSet accelerationStructureWrite{};
	accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	// The specialized acceleration structure descriptor has to be chained
	accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
	accelerationStructureWrite.dstSet = rtDescriptorSet;
	accelerationStructureWrite.dstBinding = 0;
	accelerationStructureWrite.descriptorCount = 1;
	accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };

	std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
		// Binding 0: Top level acceleration structure
		accelerationStructureWrite,
		// Binding 1: Ray tracing result image
		vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
		// Binding 2: Uniform data
		vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &uniformBuffer.descriptor),
		// Binding 4: Geometry node information SSBO
		//vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &model.geometryNodes.descriptor),
	};



	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void MyMeshClustrizingMeshopt::createUniformBuffer()
{
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&uniformBuffer,
		sizeof(uniformData),
		&uniformData));
	VK_CHECK_RESULT(uniformBuffer.map());

	updateUniformBuffers();
}

void MyMeshClustrizingMeshopt::handleResize()
{
	// Recreate image
	createStorageImage(swapChain.colorFormat, { width, height, 1 });
	// Update descriptor
	VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
	VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
	vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
	resized = false;
}

void MyMeshClustrizingMeshopt::buildCommandBuffers()
{
	if (resized)
	{
		handleResize();
	}

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[i];
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
		gpuTimer->reset(cmdBuffer);

		// build or update AS
		{
			buildBLASes(cmdBuffer);

			accelBuildPipelineBarrier(cmdBuffer);

			buildTLAS(cmdBuffer);
		}

		/*
			Dispatch the ray tracing commands
		*/
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);

		// push constant - vertex/index device addressvkCmdPushConstants(
		vkCmdPushConstants(cmdBuffer, rtPipelineLayout,
			VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
			0, sizeof(PushConstantData), &pushConstantData
		);

		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0, 1, &rtDescriptorSet, 0, 0);

		VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
		gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
		vkCmdTraceRaysKHR(
			cmdBuffer,
			&shaderBindingTables.raygen.stridedDeviceAddressRegion,
			&shaderBindingTables.miss.stridedDeviceAddressRegion,
			&shaderBindingTables.hit.stridedDeviceAddressRegion,
			&emptySbtEntry,
			width,
			height,
			1);
		gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		/*
			Copy ray tracing output to swap chain image
		*/

		// Prepare current swap chain image as transfer destination
		vks::tools::setImageLayout(
			cmdBuffer,
			swapChain.images[i],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresourceRange);

		// Prepare ray tracing output image as transfer source
		vks::tools::setImageLayout(
			cmdBuffer,
			storageImage.image,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			subresourceRange);

		VkImageCopy copyRegion{};
		copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.srcOffset = { 0, 0, 0 };
		copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.dstOffset = { 0, 0, 0 };
		copyRegion.extent = { width, height, 1 };
		vkCmdCopyImage(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		// Transition swap chain image back for presentation
		vks::tools::setImageLayout(
			cmdBuffer,
			swapChain.images[i],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			subresourceRange);

		// Transition ray tracing output image back to general layout
		vks::tools::setImageLayout(
			cmdBuffer,
			storageImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			subresourceRange);

		drawUI(cmdBuffer, frameBuffers[i]);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}
}

void MyMeshClustrizingMeshopt::updateUniformBuffers()
{
	uniformData.projInverse = glm::inverse(camera.matrices.perspective);
	uniformData.viewInverse = glm::inverse(camera.matrices.view);
	// This value is used to accumulate multiple frames into the finale picture
	// It's required as ray tracing needs to do multiple passes for transparency
	// In this sample we use noise offset by this frame index to shoot rays for transparency into different directions
	// Once enough frames with random ray directions have been accumulated, it looks like proper transparency
	uniformData.frame++;
	memcpy(uniformBuffer.mapped, &uniformData, sizeof(uniformData));
}

void MyMeshClustrizingMeshopt::getEnabledFeatures()
{
	// Enable features required for ray tracing using feature chaining via pNext		
	enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

	enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;

	enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
	enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

	physicalDeviceDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
	physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.pNext = &enabledAccelerationStructureFeatures;

	deviceCreatepNextChain = &physicalDeviceDescriptorIndexingFeatures;

	enabledFeatures.samplerAnisotropy = VK_TRUE;
}

void MyMeshClustrizingMeshopt::loadAssets()
{
#if BINDLESS_SKINNING
	g_loadingFlag = (myglTF::FileLoadingFlags)(uint64_t)(g_loadingFlag | myglTF::FileLoadingFlags::CombinedMeshBuffer);
#endif
	loader.LoadFromFile(vulkanDevice, &model, "D:\\Documents\\Blender\\Exports\\Models\\dragon.gltf");
	//loader.LoadFromFile(vulkanDevice, &model, "D:\\Documents\\Blender\\Exports\\MocapGuy.gltf");

	// Init Model's device resources - this should be done by vulkanResourceManager
	{
		VkDeviceSize bufferSize = model.m_isSkeletalMesh ? myglTF::MySimpleGltfLoader::SkeletalMesh::CONSTANT_DATA_SIZE : myglTF::MySimpleGltfLoader::Mesh::CONSTANT_DATA_SIZE;

		myglTF::Create_ModelDeviceResources(device, model);

		// T-Pose Vertex Buffer
		VkDeviceSize vertexBufferSize = sizeof(myglTF::MySimpleGltfLoader::VertexSimple) * model.m_vertices.size();
		vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vertexBufferSize, &model.m_vertexBuffer.vkBuffer, &model.m_vertexBuffer.vkMemory, graphicsQueue, model.m_vertices.data());
		model.m_vertexBuffer.device = device;
		model.m_vertexBuffer.deviceAddress = getBufferDeviceAddress(model.m_vertexBuffer.vkBuffer);
		model.m_vertexBuffer.bufferSize = vertexBufferSize;
	}

	model.UpdateAnimation(6.35446882);

}
void MyMeshClustrizingMeshopt::enableExtensions()
{
	MyVulkanRTBase::enableExtensions();
}

void MyMeshClustrizingMeshopt::prepare()
{
	MyVulkanRTBase::prepare();
#if MEASURE_MODE
#if defined(_WIN32)
	setupConsole("Vulkan example");
#endif
#endif
#if _DEBUG & !SKIP_SHADER_COMIPLE  // compile shaders
	std::string batchPath = getShadersPath() + "myMeshClusteringCuda/ShaderCompile.bat";
	system(batchPath.c_str());
	std::cout << "\t...current project's shaders compile completed.\n";
#endif
	loadAssets();


	// Meshopt Cluster Building
	{
		myUtils::CPUTimer timer("Meshopt Clustering");
		timer.start();
		static constexpr uint32_t clusterVerticesMax = 128;
		size_t minTriangles = (clusterMaxSize / 4) & ~3; // allow smaller clusters to be generated when that significantly improves their bounds
		size_t maxVerticesPerMeshlet = clusterVerticesMax; // Same for MeshShader
		size_t maxIndicesPerMeshlet = minTriangles; // If MeshShader:124
		float clusterMeshoptSpatialFill = 0.5f;

		meshopt_optimizeVertexCache(model.m_indices.data(), model.m_indices.data(), model.m_indices.size(), model.m_vertices.size());

		std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(model.m_indices.size(), clusterVerticesMax, minTriangles));

		std::vector<uint32_t> clusterLocalVertices(meshlets.size() * clusterVerticesMax);
		std::vector<uint8_t> clusterLocalIndices(meshlets.size() * clusterMaxSize * 3);

		size_t numClusters = meshopt_buildMeshletsSpatial(
			meshlets.data(),
			clusterLocalVertices.data(),
			clusterLocalIndices.data(),
			model.m_indices.data(),
			model.m_indices.size(),
			reinterpret_cast<const float*>(model.m_vPositions.data()),
			model.m_vPositions.size(),
			sizeof(glm::vec3),
			std::min(255u, clusterVerticesMax),
			minTriangles,
			clusterMaxSize,
			clusterMeshoptSpatialFill);
		timer.record(true);


		if (numClusters)
		{
			myUtils::ScopedCPUTimer timer("Clustering - Update Index Buffer");
			clusters.resize(numClusters);
			clusters.shrink_to_fit();

			// Fill Cluster Data
			uint64_t clusterIdx = 0;
			for (; clusterIdx < numClusters; ++clusterIdx)
			{
				meshopt_Meshlet& meshlet = meshlets[clusterIdx];
				gmc::Cluster& cluster = clusters[clusterIdx].gmcCluster;
				cluster = {};
				cluster.triangleCount = static_cast<uint16_t>(meshlet.triangle_count);
				cluster.vertexCount = static_cast<uint16_t>(meshlet.vertex_count);
				cluster.triangleOffset = meshlet.triangle_offset;
				cluster.vertexOffset = meshlet.vertex_offset;

			}
			gmc::Cluster& lastCluster = clusters[numClusters - 1].gmcCluster;

			clusterLocalVertices.resize(lastCluster.vertexOffset + lastCluster.vertexCount);
			clusterLocalVertices.shrink_to_fit();
			clusterLocalIndices.resize(lastCluster.triangleOffset + lastCluster.triangleCount * 3);
			clusterLocalIndices.shrink_to_fit();

			uint32_t triangleOffsetInMesh = 0u;
			// Flow : LocalIndex -> LocalVertex -> GlobalVertex -> GlobalIndex
			for (uint64_t clusterIdx = 0; clusterIdx < clusters.size(); ++clusterIdx)
			{
				gmc::Cluster& cluster = clusters[clusterIdx].gmcCluster;
				clusters[clusterIdx].firstTriangle = triangleOffsetInMesh;
				triangleOffsetInMesh += cluster.triangleCount;
				// cluster : | tri0 | tri1 | "tri2" | tri3 | tri4 | ,,, | 
				for (uint32_t t = 0; t < cluster.triangleCount; ++t) // per triangle in Cluster
				{
					// cur 3 "local vertices(uint3)" of triangle in clusrter
					glm::uvec3 indicesForLocalIndexBuffer = {
						clusterLocalIndices[cluster.triangleOffset + (t * 3) + 0],
						clusterLocalIndices[cluster.triangleOffset + (t * 3) + 1],
						clusterLocalIndices[cluster.triangleOffset + (t * 3) + 2] };

					assert(indicesForLocalIndexBuffer.x < cluster.vertexCount);
					assert(indicesForLocalIndexBuffer.y < cluster.vertexCount);
					assert(indicesForLocalIndexBuffer.z < cluster.vertexCount);

					glm::uvec3 globalVertices = {};

					// globalTriangle == 3 localVertices
					glm::uvec3 curLocalIndices = {
						cluster.vertexOffset + indicesForLocalIndexBuffer.x,
						cluster.vertexOffset + indicesForLocalIndexBuffer.y,
						cluster.vertexOffset + indicesForLocalIndexBuffer.z };


					// 3 local vertices == 3 global indices
					if (true) // !m_config.clusterDedicatedVertices from scene.cpp(https://github.com/nvpro-samples/vk_animated_clusters/blob/main/src/scene.cpp)
					{
						// need one more indirection
						globalVertices = { clusterLocalVertices[curLocalIndices.x], clusterLocalVertices[curLocalIndices.y],
										  clusterLocalVertices[curLocalIndices.z] };
					}
					else
					{
						// cur 3 local indices is 3 global indicess
						globalVertices = curLocalIndices;
						//globalVertices = {
						//   +cluster.firstLocalVertex + indicesForLocalIndxBuffer.x,
						//   +cluster.firstLocalVertex + indicesForLocalIndxBuffer.y,
						//   +cluster.firstLocalVertex + indicesForLocalIndxBuffer.z };
					}

					// write into original Index Array
					model.m_indices[(clusters[clusterIdx].firstTriangle + t) * 3 + 0] = globalVertices.x;
					model.m_indices[(clusters[clusterIdx].firstTriangle + t) * 3 + 1] = globalVertices.y;
					model.m_indices[(clusters[clusterIdx].firstTriangle + t) * 3 + 2] = globalVertices.z;
				}
			}
			blasPoolSet.numActiveBlases = numBLASesMax = numClusters;
		}

	}


	// Create Vertex/Index Buffer
	{
		// Vertices (Fixed Vertices)
		size_t vertexSize = sizeof(myglTF::MySimpleGltfLoader::VertexSimple);
		size_t vertexBufferSize = model.m_vertices.size() * vertexSize;

		vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vertexBufferSize, &model.m_vertexBuffer.vkBuffer, &model.m_vertexBuffer.vkMemory, graphicsQueue, model.m_vertices.data());
		model.m_vertexBuffer.deviceAddress = getBufferDeviceAddress(model.m_vertexBuffer.vkBuffer);
		model.m_vertexBuffer.bufferSize = vertexBufferSize;
		model.m_vertexBuffer.bufferSize = vertexBufferSize;
		model.m_vertexBuffer.descriptor = { model.m_vertexBuffer.vkBuffer, 0,  model.m_vertexBuffer.bufferSize };
		
		// Vertices (Deforming Vertices)
		vertexSize = sizeof(glm::vec3);
		vertexBufferSize = model.m_vertices.size() * vertexSize;

		vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vertexBufferSize, &model.m_deformingVertexBuffer.vkBuffer, &model.m_deformingVertexBuffer.vkMemory, graphicsQueue, model.m_vPositions.data());
		model.m_deformingVertexBuffer.deviceAddress = getBufferDeviceAddress(model.m_deformingVertexBuffer.vkBuffer);
		model.m_deformingVertexBuffer.bufferSize = vertexBufferSize;
		model.m_deformingVertexBuffer.descriptor = { model.m_deformingVertexBuffer.vkBuffer, 0, model.m_deformingVertexBuffer.bufferSize };


		size_t indexBufferSize = model.m_indices.size() * sizeof(uint32_t);

		vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			indexBufferSize, &model.m_indexBuffer.vkBuffer, &model.m_indexBuffer.vkMemory, graphicsQueue, model.m_indices.data());
		model.m_indexBuffer.deviceAddress = getBufferDeviceAddress(model.m_indexBuffer.vkBuffer);
		model.m_indexBuffer.bufferSize = indexBufferSize;
	}
	// make timer
	gpuTimer = std::make_unique<GPUTimer>(device, deviceProperties.limits.timestampPeriod, (3/*fixedBlasNum*/) * 2);
	gpuTimer->init();

	// Create the acceleration structures used to render the ray traced scene
	VkCommandBuffer accelBuildCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	if (animComputePass)
	{
		animComputePass->buildCommandBuffer(accelBuildCmdBuffer, 0);
		vulkanDevice->flushCommandBuffer(accelBuildCmdBuffer, graphicsQueue, false);
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(accelBuildCmdBuffer, &cmdBufferBeginInfo));
		
	}
		

	//initBlasPool(); // BLAS, TLAS
	initBlases();
	initTLAS();

	// Calc Acceleration Structure Size

	std::cout << "Acceleration Sturcture Info\n";
	std::cout << "Num Blases : " << dynamicBLASes.size() << "\n";
	std::cout << "BLASes Size : " << totalBlasSize / 1024.0 << " KB\n";
	std::cout << "TLAS Size : " << tlasSize / 1024.0 << " KB\n";
	std::cout << "Total AS Size : " << (totalBlasSize + tlasSize) / 1024.0 << " KB\n";
	std::cout << "=================================\n";


	buildBLASes(accelBuildCmdBuffer);
	accelBuildPipelineBarrier(accelBuildCmdBuffer);
	buildTLAS(accelBuildCmdBuffer);
	vulkanDevice->flushCommandBuffer(accelBuildCmdBuffer, graphicsQueue);

	createComputePipeline();
	createStorageImage(swapChain.colorFormat, { width, height, 1 });
	createUniformBuffer();
	createRayTracingPipeline();
	createShaderBindingTables();
	createDescriptorSets();
	buildCommandBuffers();


	prepared = true;
}

void MyMeshClustrizingMeshopt::draw()
{
	MyVulkanBase::prepareFrame();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	MyVulkanBase::submitFrame();
}

void MyMeshClustrizingMeshopt::render()
{
	if (!prepared)
		return;


	//if (0)
	if (!paused)
	{
		// update all animations, ALl skeletal mesh should have a single animation.
		//model.UpdateAnimation(frameTimer);

		VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		static float accTime = 0.f;
		accTime += frameTimer;
		if (animComputePass)
			animComputePass->buildCommandBuffer(cmdBuffer, accTime);

		vulkanDevice->flushCommandBuffer(cmdBuffer, graphicsQueue);

		updateUniformBuffers();
		uniformData.frame = -1;
	}
	draw();
#if MEASURE_MODE
	static uint32_t frameCount, accFPS = 0;
	static float accBuildBLASTime, accBuildTLASTime, accTraceTime = 0.f;

	++frameCount;

	if (frameCount >= WARMINGUP_FRAME && frameCount <= MEASURE_END_FRAME)
	{
		accFPS += lastFPS;
		const std::vector<float> gpuTimerResult = gpuTimer->timerResult();
		accBuildBLASTime += gpuTimerResult[0];
		accBuildTLASTime += gpuTimerResult[1];
		accTraceTime += gpuTimerResult[2];

		if (frameCount == MEASURE_END_FRAME)
		{
			float blasAvg = accBuildBLASTime / MEASURE_FRAME_COUNT;
			float tlasAvg = accBuildTLASTime / MEASURE_FRAME_COUNT;
			float rtAvg = accTraceTime / MEASURE_FRAME_COUNT;
			float fpsAvg = (float)accFPS / MEASURE_FRAME_COUNT;

			printf("Clustering - Meshopt, Measured Frame Count: %d\n", MEASURE_FRAME_COUNT);
			printf("Average BLAS Build Time = %.3f (ms)\n", blasAvg);
			printf("Average TLAS Build Time = %.3f (ms)\n", tlasAvg);
			printf("Average Total AS Build Time = %.3f (ms)\n", blasAvg + tlasAvg);
			printf("Average Tracing Time = %.3f (ms)\n", rtAvg);
			printf("\n");
			printf("Average FPS = %.3f fps (%.3f ms)\n", fpsAvg, 1000.f / fpsAvg);
			printf("=================================\n");
		}
	}
#endif

}

void MyMeshClustrizingMeshopt::OnUpdateUIOverlay(vks::UIOverlay* overlay)
{
}
