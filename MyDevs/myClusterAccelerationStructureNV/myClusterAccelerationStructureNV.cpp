/*
* Vulkan Example - Scene rendering
*
* Copyright (C) 2020-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Ray tracing Cluster Acceleration Sturcture basic, without using CLAS templates
* - clas template (x) , implicit build(o)
* This work continues from the "MyClusterAccelerationStructureNV" implementation.
* 
* This sample comes with a tutorial, see the README.md in this folder
*/

#include "myClusterAccelerationStructureNV.h"
#include "myIncludesCPUGPU.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::MakeClusters | myglTF::FileLoadingFlags::PreTransformVertices);


MyClusterAccelerationStructureNV::MyClusterAccelerationStructureNV()
{
	title = "MyClusterAccelerationStructureNV";
	camera.type = Camera::CameraType::firstperson;
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
	camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
	camera.setTranslation(glm::vec3(0.0f, -0.1f, -1.0f));

	enableExtensions();

	// Buffer device address requires the 64-bit integer feature to be enabled
	enabledFeatures.shaderInt64 = VK_TRUE;
}

MyClusterAccelerationStructureNV::~MyClusterAccelerationStructureNV()
{
	if (device) {
		// delete scratches
		{
			deleteScratchBuffer(clasScratchBuffer);
			deleteScratchBuffer(clusteredBlasScratchBuffer);
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
			vkDestroyBuffer(vulkanDevice->logicalDevice, clusterBuildInfoBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, clusterBuildInfoBuffer.memory, nullptr);
			vkDestroyBuffer(vulkanDevice->logicalDevice, clusterDstAddressBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, clusterDstAddressBuffer.memory, nullptr);
			vkDestroyBuffer(vulkanDevice->logicalDevice, clusterSizeBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, clusterSizeBuffer.memory, nullptr);
			vkDestroyBuffer(vulkanDevice->logicalDevice, clusteredBlasBuildInfoBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, clusteredBlasBuildInfoBuffer.memory, nullptr);
			vkDestroyBuffer(vulkanDevice->logicalDevice, clusteredBlasDstAddressBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, clusteredBlasDstAddressBuffer.memory, nullptr);
			vkDestroyBuffer(vulkanDevice->logicalDevice, clusteredBlasSizeBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, clusteredBlasSizeBuffer.memory, nullptr);


			vkDestroyBuffer(vulkanDevice->logicalDevice, blasInstancesBuffer.buffer, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, blasInstancesBuffer.memory, nullptr);
		}


		vkDestroyPipeline(device, rtPipeline, nullptr);
		vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, rtDescriptorSetLayout, nullptr);
		deleteStorageImage();
		// Delete Acceleration Structures
		{
			deleteAccelerationStructure(CLAS);
			deleteAccelerationStructure(clusteredBLASes);
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

void MyClusterAccelerationStructureNV::createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure,
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

void MyClusterAccelerationStructureNV::initCLASes()
{
	const uint32_t numTotalClusters = model.m_numTotalClusters;
	//const uint32_t numTotalClusters = model.m_numTotalClusters;
	// update VkClusterAccelerationStructureTriangleClusterInputNV
	{
		clasInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
		clasInput.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		clasInput.maxClusterTriangleCount = model.m_clusterTriangleMax;
		clasInput.maxClusterVertexCount = model.m_clusterVertexMax;
		clasInput.minPositionTruncateBitCount = 0;
		for (auto& input : model.clusteredGeometryNodes)
		{
			clasInput.maxTotalVertexCount += input.numVertices;
			clasInput.maxTotalTriangleCount += input.numTriangles;
		}
	}

	VkClusterAccelerationStructureInputInfoNV clasInputInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
	clasInputInfo.maxAccelerationStructureCount = numTotalClusters;
	clasInputInfo.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
	clasInputInfo.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
	clasInputInfo.opInput.pTriangleClusters = &clasInput;
	clasInputInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetClusterAccelerationStructureBuildSizesNV(device, &clasInputInfo, &buildSizesInfo);
	clasScratchSizeMax = std::max(clasScratchSizeMax, buildSizesInfo.buildScratchSize);


	std::vector<VkClusterAccelerationStructureBuildTriangleClusterInfoNV> clusterBuildInfos(numTotalClusters, {});
	std::vector<uint64_t> clusterDstAddresses(numTotalClusters, 0);

	uint32_t infoIdx = 0;
	for (uint32_t geometryNodeIdx = 0; geometryNodeIdx < model.clusteredGeometryNodes.size(); ++geometryNodeIdx)
	{
		const ClusteredGeometryNodeRT& geometryNode = model.clusteredGeometryNodes[geometryNodeIdx];
		const myglTF::ModelRT::PerMeshClustersBuildData& perMeshClusterData = model.perMeshClustersBuildDatas[geometryNodeIdx];
		for (uint32_t i = 0; i < perMeshClusterData.clustersCPU.size(); ++i)
		{
			const ClusterRT& cluster = perMeshClusterData.clustersCPU[i];

			VkClusterAccelerationStructureBuildTriangleClusterInfoNV& refBuildInfo = clusterBuildInfos[infoIdx++];

			refBuildInfo.clusterID = i;
			refBuildInfo.vertexCount = cluster.numVertices;
			refBuildInfo.triangleCount = cluster.numTriangles;
			refBuildInfo.baseGeometryIndexAndGeometryFlags.geometryFlags = VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;

			refBuildInfo.vertexBuffer = geometryNode.vertexBufferDeviceAddress;
			refBuildInfo.vertexBufferStride = sizeof(myglTF::VertexSimple); // TODO : if animated, vertexSkining type

			//refBuildInfo.indexBuffer = geometryNode.indexBufferDeviceAddress + cluster.firstLocalTriangle * sizeof(uint32_t) * 3;
			refBuildInfo.indexBuffer = geometryNode.indexBufferDeviceAddress + (perMeshClusterData.indexStartOffset + cluster.firstTriangle * 3) * sizeof(uint32_t);
			refBuildInfo.indexBufferStride = sizeof(uint32_t);
			refBuildInfo.indexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV;

			refBuildInfo.positionTruncateBitCount = positionTruncateBits;
		}
	}


	// create CLAS buffer
	createAccelerationStructureBuffer(CLAS, buildSizesInfo, static_cast<VkBufferUsageFlagBits>(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
		| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));
	CLAS.deviceAddress = getBufferDeviceAddress(CLAS.buffer);

	// Indirect Argument Buffer - cluster buildInfo
	vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV) * numTotalClusters,
		&clusterBuildInfoBuffer.buffer, &clusterBuildInfoBuffer.memory, graphicsQueue, clusterBuildInfos.data());
	clusterBuildInfoBuffer.deviceAddress = getBufferDeviceAddress(clusterBuildInfoBuffer.buffer);
	clusterBuildInfoBuffer.bufferSize = sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV) * numTotalClusters;
	
	// Indirect Argument Buffer - cluster dst address
	vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		sizeof(uint64_t) * numTotalClusters,
		&clusterDstAddressBuffer.buffer, &clusterDstAddressBuffer.memory);
	clusterDstAddressBuffer.deviceAddress = getBufferDeviceAddress(clusterDstAddressBuffer.buffer);
	clusterDstAddressBuffer.bufferSize = sizeof(uint64_t) * numTotalClusters;
	
	// Indirect Argument Buffer - cluster buildInfo
	vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		sizeof(uint32_t) * numTotalClusters,
		&clusterSizeBuffer.buffer, &clusterSizeBuffer.memory);
	clusterSizeBuffer.deviceAddress = getBufferDeviceAddress(clusterSizeBuffer.buffer);
	clusterSizeBuffer.bufferSize = sizeof(uint32_t) * numTotalClusters;

	// create scratch buffer
	clasScratchBuffer = createScratchBuffer(clasScratchSizeMax);
}


void MyClusterAccelerationStructureNV::initBLASes()
{
	// Use transform matrices from the glTF nodes
	std::vector<VkTransformMatrixKHR> transformMatrices{}; // per node
	for (auto node : model.linearNodes) {
		if (node->mesh)
		{
			for (auto primitive : node->mesh->primitives) {
				if (primitive->indexCount > 0) {
					VkTransformMatrixKHR transformMatrix{};
					auto m = glm::mat3x4(glm::transpose(node->getMatrix()));
					memcpy(&transformMatrix, (void*)&m, sizeof(glm::mat3x4));
					transformMatrices.push_back(transformMatrix);
				}
			}
		}
	}

	// Transform buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&transformBuffer,
		static_cast<uint32_t>(transformMatrices.size()) * sizeof(VkTransformMatrixKHR),
		transformMatrices.data()));

	uint32_t nodeIdx = 0u; // node containing mesh
	for (auto node : model.linearNodes)
	{
		if (node->mesh)
		{
			const bool isDeformable = node->skin; // isDynamicBlas?
			if (isDeformable)
				dynamicPerBlasBuildInfos.push_back(PerBLASBuildInfo{});
			else
				staticPerBlasBuildInfos.push_back(PerBLASBuildInfo{});

			// avoid dangling pointer due to moving array;
			PerBLASBuildInfo& refPerBlasBuildInfo = isDeformable ? dynamicPerBlasBuildInfos.back() : staticPerBlasBuildInfos.back();

			const myglTF::Mesh* mesh = node->mesh;
			// Build
			// One geometry per glTF node, so we can index materials using gl_GeometryIndexEXT
			std::vector<uint32_t> maxPrimitiveCounts{};
			//std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pBuildRangeInfos{};

			for (auto primitive : mesh->primitives) {
				if (primitive->indexCount > 0) {
					VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
					VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
					VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

					vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(model.vertices.buffer);// +primitive->firstVertex * sizeof(vkglTF::Vertex);
					indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(model.indices.buffer) + primitive->firstIndex * sizeof(uint32_t);
					transformBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(transformBuffer.buffer) + nodeIdx * sizeof(VkTransformMatrixKHR);

					VkAccelerationStructureGeometryKHR asGeometry{}; // per gltf primitive
					asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
					asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
					asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
					asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
					asGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
					asGeometry.geometry.triangles.maxVertex = primitive->vertexCount + 1;
					asGeometry.geometry.triangles.vertexStride = sizeof(myglTF::VertexSimple);
					asGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
					asGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
					asGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;
					refPerBlasBuildInfo.asGeometries.push_back(asGeometry);
					maxPrimitiveCounts.push_back(primitive->indexCount / 3);

					VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
					buildRangeInfo.firstVertex = 0;
					buildRangeInfo.primitiveOffset = 0; // primitive->firstIndex * sizeof(uint32_t);
					buildRangeInfo.primitiveCount = primitive->indexCount / 3;
					buildRangeInfo.transformOffset = 0;
					refPerBlasBuildInfo.buildRangeInfos.push_back(buildRangeInfo);
				}
			}

			// Get size info
			VkAccelerationStructureBuildGeometryInfoKHR& accelerationStructureBuildGeometryInfo = refPerBlasBuildInfo.asBuildGeometryInfo;
			accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
#if FORCE_STATIC_SCENE
			accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
#else
			accelerationStructureBuildGeometryInfo.flags = isDeformable ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
				: VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
#endif
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
			// TODO: if static -> fix to buildScratchSize?
			refPerBlasBuildInfo.blasScratchSizeMax = std::max(accelerationStructureBuildSizesInfo.buildScratchSize, accelerationStructureBuildSizesInfo.updateScratchSize);
			if (isDeformable)
			{
				dynamicBLASes.push_back(blas);
			}
			else
			{
				staticBLASes.push_back(blas);
			}
		}
	}
}

void MyClusterAccelerationStructureNV::initClusteredBLASes()
{
	uint32_t numGeometryNodes = model.clusteredGeometryNodes.size(); // == num meshes == num blases
	uint32_t numBlases = numGeometryNodes;
	std::vector<VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV> blasBuildInfos(numGeometryNodes, {});

	uint64_t clusterOffset = 0u;
	for (uint32_t geometryNodeIdx = 0; geometryNodeIdx < numGeometryNodes; ++geometryNodeIdx)
	{
		const ClusteredGeometryNodeRT& geometryNode = model.clusteredGeometryNodes[geometryNodeIdx];

		blasBuildInfos[geometryNodeIdx].clusterReferences = clusterDstAddressBuffer.deviceAddress + clusterOffset * sizeof(VkDeviceAddress);
		blasBuildInfos[geometryNodeIdx].clusterReferencesCount = geometryNode.numClusters;
		blasBuildInfos[geometryNodeIdx].clusterReferencesStride = sizeof(uint64_t);
		clusterOffset += geometryNode.numClusters;
	}

	vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV) * numBlases,
		&clusteredBlasBuildInfoBuffer.buffer, &clusteredBlasBuildInfoBuffer.memory, graphicsQueue, blasBuildInfos.data());
	clusteredBlasBuildInfoBuffer.deviceAddress = getBufferDeviceAddress(clusteredBlasBuildInfoBuffer.buffer);
	clusteredBlasBuildInfoBuffer.bufferSize = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV) * numBlases;

	vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		sizeof(uint64_t) * numBlases,
		&clusteredBlasDstAddressBuffer.buffer, &clusteredBlasDstAddressBuffer.memory);
	clusteredBlasDstAddressBuffer.deviceAddress = getBufferDeviceAddress(clusteredBlasDstAddressBuffer.buffer);
	clusteredBlasDstAddressBuffer.bufferSize = sizeof(uint64_t) * numBlases;

	vulkanDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		sizeof(uint32_t) * numBlases,
		&clusteredBlasSizeBuffer.buffer, &clusteredBlasSizeBuffer.memory);
	clusteredBlasSizeBuffer.deviceAddress = getBufferDeviceAddress(clusteredBlasSizeBuffer.buffer);
	clusteredBlasSizeBuffer.bufferSize = sizeof(uint32_t) * numBlases;

	clusteredBlasInput.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV;
	clusteredBlasInput.maxClusterCountPerAccelerationStructure = model.m_perMeshClusterMax;
	clusteredBlasInput.maxTotalClusterCount = model.m_numTotalClusters;

	VkClusterAccelerationStructureInputInfoNV clusteredBlasInputInfo{};
	clusteredBlasInputInfo.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV;
	clusteredBlasInputInfo.maxAccelerationStructureCount = numBlases;
	clusteredBlasInputInfo.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
	clusteredBlasInputInfo.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
	clusteredBlasInputInfo.opInput.pClustersBottomLevel = &clusteredBlasInput;
	clusteredBlasInputInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;


	VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetClusterAccelerationStructureBuildSizesNV(device, &clusteredBlasInputInfo, &buildSizeInfo);
	clusteredBlasScratchSizeMax = std::max(clusteredBlasScratchSizeMax, buildSizeInfo.buildScratchSize);


	// create ClusteredBLASes buffer
	createAccelerationStructureBuffer(clusteredBLASes, buildSizeInfo, static_cast<VkBufferUsageFlagBits>(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR));
	clusteredBLASes.deviceAddress = getBufferDeviceAddress(clusteredBLASes.buffer);

	// create scratch buffer
	clusteredBlasScratchBuffer = createScratchBuffer(clusteredBlasScratchSizeMax);
}

void MyClusterAccelerationStructureNV::initTLAS()
{
	VkTransformMatrixKHR transformMatrix = {
		   1.0f, 0.0f, 0.0f, 0.0f,
		   0.0f, -1.0f, 0.0f, 0.0f,
		   0.0f, 0.0f, 1.0f, 0.0f };

	for (auto& blas : staticBLASes)
	{
		VkAccelerationStructureInstanceKHR blasInstance{};
		blasInstance.transform = transformMatrix;
		blasInstance.instanceCustomIndex = 0;
		blasInstance.mask = 0xFF;
		blasInstance.instanceShaderBindingTableRecordOffset = 0;
		blasInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		blasInstance.accelerationStructureReference = blas.deviceAddress;
		blasInstances.push_back(blasInstance);
	}
	for (auto& blas : dynamicBLASes)
	{
		VkAccelerationStructureInstanceKHR blasInstance{};
		blasInstance.transform = transformMatrix;
		blasInstance.instanceCustomIndex = 0;
		blasInstance.mask = 0xFF;
		blasInstance.instanceShaderBindingTableRecordOffset = 0;
		blasInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		blasInstance.accelerationStructureReference = blas.deviceAddress;
		blasInstances.push_back(blasInstance);
	}
	// for clustered BLAS
	{
		VkAccelerationStructureInstanceKHR blasInstance{};
		blasInstance.transform = transformMatrix;
		blasInstance.instanceCustomIndex = 0;
		blasInstance.mask = 0xFF;
		blasInstance.instanceShaderBindingTableRecordOffset = 0;
		blasInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		blasInstance.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;

		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = clusteredBLASes.handle;
		blasInstance.accelerationStructureReference = 0;// Upadated during compute shader 
		for (uint32_t i = 0; i < model.clusteredGeometryNodes.size(); ++i)
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

void MyClusterAccelerationStructureNV::buildCLASes(VkCommandBuffer cmdBuffer)
{
	VkClusterAccelerationStructureCommandsInfoNV cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV;
	VkClusterAccelerationStructureInputInfoNV inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV;

	// setup cluster build inputs
	inputInfo.maxAccelerationStructureCount = model.m_numTotalClusters;

	// use implicit if we don't have per render instance cluster buffers
	inputInfo.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
	inputInfo.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;

	inputInfo.opInput.pTriangleClusters = &clasInput;
	inputInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

	cmdInfo.dstImplicitData = CLAS.deviceAddress;  // can be zero if explicit

	cmdInfo.dstAddressesArray.deviceAddress = clusterDstAddressBuffer.deviceAddress;
	cmdInfo.dstAddressesArray.size = clusterDstAddressBuffer.bufferSize;
	cmdInfo.dstAddressesArray.stride = sizeof(uint64_t);

	cmdInfo.dstSizesArray.deviceAddress = clusterSizeBuffer.deviceAddress;
	cmdInfo.dstSizesArray.size = clusterSizeBuffer.bufferSize;
	cmdInfo.dstSizesArray.stride = sizeof(uint32_t);

	cmdInfo.srcInfosArray.deviceAddress = clusterBuildInfoBuffer.deviceAddress;
	cmdInfo.srcInfosArray.size = clusterBuildInfoBuffer.bufferSize;
	cmdInfo.srcInfosArray.stride = sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV);

	cmdInfo.scratchData = clasScratchBuffer.deviceAddress;
	cmdInfo.input = inputInfo;

	vkCmdBuildClusterAccelerationStructureIndirectNV(cmdBuffer, &cmdInfo);
}

void MyClusterAccelerationStructureNV::buildBLASes()
{
	// TODO build Static-Dynamic BLAS Parallelly
	uint32_t numStaticBlases = staticBLASes.size();
	uint32_t numDynamicBlases = dynamicBLASes.size(); // for Deformable Mesh

	VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	const bool isFirstBuild = (numStaticBlases && staticBLASes[0].handle == VK_NULL_HANDLE)
		|| (numDynamicBlases && dynamicBLASes[0].handle == VK_NULL_HANDLE);
	//gpuTimer->reset(commandBuffer);
	//gpuTimer->record(commandBuffer, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0);

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
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			numDynamicBlases,
			dynamicBlasBuildingSets.buildGeometryInfos.data(),
			dynamicBlasBuildingSets.buildRangeInfosArray.data());

		vulkanDevice->flushCommandBuffer(commandBuffer, graphicsQueue, false);
		// TODO get rid of this if possible
		{
			VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));
		}
	}
	//gpuTimer->record(commandBuffer, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 1);

	// static blas
	if (numStaticBlases)
	{
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			numStaticBlases,
			staticBlasBuildingSets.buildGeometryInfos.data(),
			staticBlasBuildingSets.buildRangeInfosArray.data());

		vulkanDevice->flushCommandBuffer(commandBuffer, graphicsQueue);
	}

	if (isFirstBuild)
	{
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
}

void MyClusterAccelerationStructureNV::buildClusteredBLASes(VkCommandBuffer cmdBuffer)
{
	VkClusterAccelerationStructureCommandsInfoNV cmdInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV };
	VkClusterAccelerationStructureInputInfoNV inputInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };

	// setup blas inputs
	inputInfo.maxAccelerationStructureCount = model.clusteredGeometryNodes.size();
	inputInfo.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
	inputInfo.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
	inputInfo.opInput.pClustersBottomLevel = &clusteredBlasInput;
	inputInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

	// we feed the generated blas addresses directly into the ray instances
	cmdInfo.dstAddressesArray.deviceAddress = clusteredBlasDstAddressBuffer.deviceAddress;
	cmdInfo.dstAddressesArray.size = clusteredBlasDstAddressBuffer.bufferSize;
	cmdInfo.dstAddressesArray.stride = sizeof(VkDeviceAddress);

	cmdInfo.dstSizesArray.deviceAddress = clusteredBlasSizeBuffer.deviceAddress;
	cmdInfo.dstSizesArray.size = clusteredBlasSizeBuffer.bufferSize;
	cmdInfo.dstSizesArray.stride = sizeof(uint32_t);

	cmdInfo.srcInfosArray.deviceAddress = clusteredBlasBuildInfoBuffer.deviceAddress;
	cmdInfo.srcInfosArray.size = clusteredBlasBuildInfoBuffer.bufferSize;
	cmdInfo.srcInfosArray.stride = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV);

	// in implicit mode we provide one big chunk from which outputs are sub-allocated
	cmdInfo.dstImplicitData = clusteredBLASes.deviceAddress;

	cmdInfo.scratchData = clusteredBlasScratchBuffer.deviceAddress;
	cmdInfo.input = inputInfo;
	vkCmdBuildClusterAccelerationStructureIndirectNV(cmdBuffer, &cmdInfo);
}


void MyClusterAccelerationStructureNV::buildTLAS(VkCommandBuffer cmdBuffer)
{
	const bool isFirstBuild = (TLAS.handle == VK_NULL_HANDLE);

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

	vkCmdBuildAccelerationStructuresKHR(
		cmdBuffer,
		1,
		&tlasBuildGeometryInfo,
		accelerationBuildStructureRangeInfos.data());

	// after first build complete
	if (isFirstBuild)
	{
		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = TLAS.handle;
		TLAS.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationDeviceAddressInfo);
	}
}

void MyClusterAccelerationStructureNV::createShaderBindingTables()
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


void MyClusterAccelerationStructureNV::createRayTracingPipeline()
{
	const uint32_t imageCount = static_cast<uint32_t>(model.textures.size());

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		// Binding 0: Top level acceleration structure
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
		// Binding 1: Ray tracing result image
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1),
		// Binding 2: Uniform buffer
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 2),
		// Binding 3: Texture image
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 3),
		// Binding 4: Geometry node information SSBO
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 4),
		// Binding 5: All Primtivies SSBO.
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 5),
		// Binding 6: All images used by the glTF model
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 6, imageCount),
	};


	// Unbound set
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT setLayoutBindingFlags{};
	setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	setLayoutBindingFlags.bindingCount = setLayoutBindings.size();
	std::vector<VkDescriptorBindingFlagsEXT> descriptorBindingFlags = {
		0,
		0,
		0,
		0,
		0,
		0,
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
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

	// Ray generation group
	{
		shaderStages.push_back(loadShader(getShadersPath() + "MyClusterAccelerationStructureNV/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
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
		shaderStages.push_back(loadShader(getShadersPath() + "MyClusterAccelerationStructureNV/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		shaderGroups.push_back(shaderGroup);
		// Second shader for shadows
		shaderStages.push_back(loadShader(getShadersPath() + "MyClusterAccelerationStructureNV/shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}

	// Closest hit group for doing texture lookups
	{
		shaderStages.push_back(loadShader(getShadersPath() + "MyClusterAccelerationStructureNV/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		// This group also uses an anyhit shader for doing transparency (see anyhit.rahit for details)
		shaderStages.push_back(loadShader(getShadersPath() + "MyClusterAccelerationStructureNV/anyhit.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
		shaderGroup.anyHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}


	// for Cluster Acceleration Structure
	VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV pipelineCLAS = {
	VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
	pipelineCLAS.allowClusterAccelerationStructure = VK_TRUE;

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
	rayTracingPipelineCI.pNext = &pipelineCLAS;

	VK_CHECK_RESULT(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &rtPipeline));
}

void MyClusterAccelerationStructureNV::createDescriptorSets()
{
	uint32_t imageCount = static_cast<uint32_t>(model.textures.size());
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(model.textures.size()) }
	};

	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountAllocInfo{};
	uint32_t variableDescCounts[] = { imageCount };
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
		vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &model.geometryNodes.descriptor),
		// Binding 5 : All Mesh Primitives
		vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &model.primitives.descriptor)
	};

	// Image descriptors for the image array
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	for (auto texture : model.textures) {
		VkDescriptorImageInfo descriptor{};
		descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		descriptor.sampler = texture.sampler;;
		descriptor.imageView = texture.view;
		textureDescriptors.push_back(descriptor);
	}

	VkWriteDescriptorSet writeDescriptorImgArray{};
	writeDescriptorImgArray.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorImgArray.dstBinding = 6;
	writeDescriptorImgArray.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writeDescriptorImgArray.descriptorCount = imageCount;
	writeDescriptorImgArray.dstSet = rtDescriptorSet;
	writeDescriptorImgArray.pImageInfo = textureDescriptors.data();
	writeDescriptorSets.push_back(writeDescriptorImgArray);

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void MyClusterAccelerationStructureNV::createUniformBuffer()
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

void MyClusterAccelerationStructureNV::handleResize()
{
	// Recreate image
	createStorageImage(swapChain.colorFormat, { width, height, 1 });
	// Update descriptor
	VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
	VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
	vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
	resized = false;
}

void MyClusterAccelerationStructureNV::buildCommandBuffers()
{
	if (resized)
	{
		handleResize();
	}

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
	{
		VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

		/*
			Dispatch the ray tracing commands
		*/
		vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);

		// push constant - vertex/index device addressvkCmdPushConstants(
		vkCmdPushConstants(drawCmdBuffers[i], rtPipelineLayout,
			VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
			0, sizeof(PushConstantData), &pushConstantData
		);

		vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0, 1, &rtDescriptorSet, 0, 0);

		VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
		vkCmdTraceRaysKHR(
			drawCmdBuffers[i],
			&shaderBindingTables.raygen.stridedDeviceAddressRegion,
			&shaderBindingTables.miss.stridedDeviceAddressRegion,
			&shaderBindingTables.hit.stridedDeviceAddressRegion,
			&emptySbtEntry,
			width,
			height,
			1);

		/*
			Copy ray tracing output to swap chain image
		*/

		// Prepare current swap chain image as transfer destination
		vks::tools::setImageLayout(
			drawCmdBuffers[i],
			swapChain.images[i],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresourceRange);

		// Prepare ray tracing output image as transfer source
		vks::tools::setImageLayout(
			drawCmdBuffers[i],
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
		vkCmdCopyImage(drawCmdBuffers[i], storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		// Transition swap chain image back for presentation
		vks::tools::setImageLayout(
			drawCmdBuffers[i],
			swapChain.images[i],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			subresourceRange);

		// Transition ray tracing output image back to general layout
		vks::tools::setImageLayout(
			drawCmdBuffers[i],
			storageImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			subresourceRange);

		drawUI(drawCmdBuffers[i], frameBuffers[i]);

		VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
	}
}

void MyClusterAccelerationStructureNV::updateUniformBuffers()
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

void MyClusterAccelerationStructureNV::getEnabledFeatures()
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

	physicalDeviceShaderImageAtomicInt64Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
	physicalDeviceShaderImageAtomicInt64Features.shaderImageInt64Atomics = VK_TRUE;
	physicalDeviceShaderImageAtomicInt64Features.pNext = &physicalDeviceDescriptorIndexingFeatures;
	clustersNV.pNext = &physicalDeviceShaderImageAtomicInt64Features;
	clustersNV.clusterAccelerationStructure = VK_TRUE;
	deviceCreatepNextChain = &clustersNV;

	enabledFeatures.samplerAnisotropy = VK_TRUE;
}

void MyClusterAccelerationStructureNV::loadAssets()
{
	//myglTF::ModelRT::memoryPropertyFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
	model.loadFromFile(getAssetPath() + "models/sponza/sponza.gltf", vulkanDevice, graphicsQueue, g_loadingFlag);
	//model.loadFromFile(getAssetPath() + "models/FlightHelmet/glTF/FlightHelmet.gltf", vulkanDevice, queue);
}

void MyClusterAccelerationStructureNV::enableExtensions()
{
	MyVulkanRTBase::enableExtensions();

	// Extensions required
	enabledDeviceExtensions.push_back(VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME);
}

void MyClusterAccelerationStructureNV::prepare()
{
	MyVulkanRTBase::prepare();

	vkGetClusterAccelerationStructureBuildSizesNV = reinterpret_cast<PFN_vkGetClusterAccelerationStructureBuildSizesNV>(vkGetDeviceProcAddr(device, "vkGetClusterAccelerationStructureBuildSizesNV"));
	vkCmdBuildClusterAccelerationStructureIndirectNV = reinterpret_cast<PFN_vkCmdBuildClusterAccelerationStructureIndirectNV>(vkGetDeviceProcAddr(device, "vkCmdBuildClusterAccelerationStructureIndirectNV"));
#if _DEBUG & !SKIP_SHADER_COMIPLE  // compile shaders
	std::string batchPath = getShadersPath() + "myClusterAccelerationStructureNV/ShaderCompile.bat";
	system(batchPath.c_str());
	std::cout << "\t...current project's shaders compile completed.\n";
#endif
	loadAssets();
	pushConstantData.sceneIndexBufferDeviceAddress = getBufferDeviceAddress(model.indices.buffer);
	pushConstantData.sceneVertexBufferDeviceAddress = getBufferDeviceAddress(model.vertices.buffer);

	// create compute pipieline
	blasUpdateComputePass = std::make_unique<MyBLASUpdateCompute>(device);
	blasUpdateComputePass->createPipeline(getShadersPath() + "MyClusterAccelerationStructureNV/clusteredBlasUpdate.comp.spv");

	// Create the acceleration structures used to render the ray traced scene
	initCLASes();
	initClusteredBLASes();
	initTLAS();

	VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	buildCLASes(cmdBuffer);

	accelBuildPipelineBarrier(cmdBuffer);

	buildClusteredBLASes(cmdBuffer);


	VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
		VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
	vkCmdPipelineBarrier(
		cmdBuffer,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_FLAGS_NONE,
		1, &memBarrier,
		0, nullptr,
		0, nullptr);

	// blas udpate dispatch commands
	{
		ClusteredBlasPushConstantData clusteredBlasPushConstants{};
		uint32_t numClusteredBlases = model.clusteredGeometryNodes.size();
		clusteredBlasPushConstants.sumCount = numClusteredBlases;
		clusteredBlasPushConstants.instanceCount = numClusteredBlases * 1; // not instancing yet
		clusteredBlasPushConstants.animated = 0;
		clusteredBlasPushConstants.blasAddresses = clusteredBlasDstAddressBuffer.deviceAddress;
		clusteredBlasPushConstants.clusteredGeometryDatas = model.geometryNodes.deviceAddress;
		clusteredBlasPushConstants.asInstances = blasInstancesBuffer.deviceAddress;
		blasUpdateComputePass->buildCommandBuffer(cmdBuffer, clusteredBlasPushConstants);
	}

	memBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	memBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	vkCmdPipelineBarrier(
		cmdBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_FLAGS_NONE,
		1, &memBarrier,
		0, nullptr,
		0, nullptr);

	buildTLAS(cmdBuffer);
	vulkanDevice->flushCommandBuffer(cmdBuffer, graphicsQueue);

	createStorageImage(swapChain.colorFormat, { width, height, 1 });
	createUniformBuffer();
	createRayTracingPipeline();
	createShaderBindingTables();
	createDescriptorSets();
	buildCommandBuffers();
	prepared = true;
}

void MyClusterAccelerationStructureNV::draw()
{
	VulkanExampleBase::prepareFrame();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	VulkanExampleBase::submitFrame();
}

void MyClusterAccelerationStructureNV::render()
{
	if (!prepared)
		return;
	updateUniformBuffers();
	if (camera.updated) {
		// If the camera's view has been updated we reset the frame accumulation
		uniformData.frame = -1;
	}

	draw();
}

MyClusterAccelerationStructureNV* myClusterAccelerationStructureNV;
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (myClusterAccelerationStructureNV != NULL)
	{
		myClusterAccelerationStructureNV->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_  HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int)
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	for (int32_t i = 0; i < __argc; i++) { MyClusterAccelerationStructureNV::args.push_back(__argv[i]); };
	myClusterAccelerationStructureNV = new MyClusterAccelerationStructureNV();
	myClusterAccelerationStructureNV->initVulkan();
	myClusterAccelerationStructureNV->setupWindow(hInstance, WndProc);
	myClusterAccelerationStructureNV->prepare();
	myClusterAccelerationStructureNV->renderLoop();
	delete(myClusterAccelerationStructureNV);
	return 0;
}