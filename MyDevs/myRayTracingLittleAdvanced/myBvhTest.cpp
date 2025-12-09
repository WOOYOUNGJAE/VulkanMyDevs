	/*
* Vulkan Example - Scene rendering
*
* Copyright (C) 2020-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Hit Count Basaed BLAS Building - Triangle BLAS, idea from GDC2025 - RTX Mega Geometry(https://youtu.be/KblmxDkaUfc?t=2807)
*
* This sample comes with a tutorial, see the README.md in this folder
*/

#include "myBvhTest.h"
#include "myIncludesCPUGPU.h"
#include "myUtils.h"
#include <set>
//#include <nvToolsExt.h>


#define BLAS_PER_CLUSTER 1
#define GEOMETRY_PER_CLUSTER !BLAS_PER_CLUSTER
#define BINDLESS_SKINNING 0
static int fixedBlasNum;

MyBvhTest::MyBvhTest()
{
	title = "MyBvhTest (" + std::to_string(width) + "x" + std::to_string(height) + ")";
	camera.type = Camera::CameraType::firstperson;
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
	camera.setTranslation(glm::vec3(-0.069f, 1.725f, -2.4f));
	camera.setRotation(glm::vec3(-17.f, -1.f, 0.f));
	camera.setRotation(glm::vec3(-10.0f, -3.0f, 0.0f));
	camera.setTranslation(glm::vec3(0.0f, 1.3f, -3.7f));

	enableExtensions();

	// Buffer device address requires the 64-bit integer feature to be enabled
	enabledFeatures.shaderInt64 = VK_TRUE;
}

MyBvhTest::~MyBvhTest()
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

void MyBvhTest::createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure,
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

void MyBvhTest::initTriangleBLASes()
{// Use transform matrices from the glTF nodes
	std::vector<VkTransformMatrixKHR> transformMatrices{}; // per node
	for (auto node : model.linearNodes) {
		if (node->mesh)
		{
			for (auto primitive : node->mesh->primitives) {
				if (primitive->indexCount > 0) {
					VkTransformMatrixKHR transformMatrix{};
					//auto m = glm::mat3x4(glm::transpose(node->getMatrix()));
					//auto m = glm::mat3x4(glm::transpose(node->getMatrix()));
					auto m = glm::mat3x4(1.f);
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

	// custom mesh
	if (openCubeMesh)
	{
		VkBuffer vertexBuffer = openCubeMesh->vertexBuffer.buffer;
		VkBuffer indexBuffer = openCubeMesh->indexBuffer.buffer;
		uint32_t primitiveCount = openCubeMesh->indexBuffer.count / 3;
		VkDeviceSize vertexStride = sizeof(glm::vec3); // position
		PerBLASBuildInfo& refPerBlasBuildInfo = staticPerBlasBuildInfos.emplace_back(PerBLASBuildInfo{});
		std::vector<uint32_t> maxPrimitiveCounts{}; //

		VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{ .deviceAddress = getBufferDeviceAddress(vertexBuffer) };
		VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{ .deviceAddress = getBufferDeviceAddress(indexBuffer) };
		VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{ .deviceAddress = getBufferDeviceAddress(transformBuffer.buffer) };

		VkAccelerationStructureGeometryKHR asGeometry{}; // per gltf primitive
		asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		asGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
		asGeometry.geometry.triangles.maxVertex = openCubeMesh->vertexBuffer.count;
		asGeometry.geometry.triangles.vertexStride = vertexStride;
		asGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
		asGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
		asGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;
		refPerBlasBuildInfo.asGeometries.push_back(asGeometry);
		maxPrimitiveCounts.push_back(primitiveCount);

		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
		buildRangeInfo.firstVertex = 0;
		buildRangeInfo.primitiveOffset = 0;
		buildRangeInfo.primitiveCount = primitiveCount;
		buildRangeInfo.transformOffset = 0;
		refPerBlasBuildInfo.buildRangeInfos.push_back(buildRangeInfo);

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR& accelerationStructureBuildGeometryInfo = refPerBlasBuildInfo.asBuildGeometryInfo;
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
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

		staticBLASes.push_back(blas);
	}

	uint32_t nodeIdx = 0u; // node containing mesh
	for (auto node : model.linearNodes)
	{
		if (node->mesh)
		{
			const bool isDeformable = node->skin; // isDynamicBlas?
			VkBuffer vertexBuffer = VK_NULL_HANDLE;
			if (isDeformable)
			{
				vertexBuffer = model.deformingVertices.buffer;
				dynamicPerBlasBuildInfos.push_back(PerBLASBuildInfo{});
			}
			else
			{
				vertexBuffer = model.vertices.buffer;
				staticPerBlasBuildInfos.push_back(PerBLASBuildInfo{});
			}

			// avoid dangling pointer due to moving array;
			PerBLASBuildInfo& refPerBlasBuildInfo = isDeformable ? dynamicPerBlasBuildInfos.back() : staticPerBlasBuildInfos.back();
			VkDeviceSize vertexStride = isDeformable ? sizeof(myglTF::VertexSkinning) : sizeof(myglTF::VertexSimple);

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

					vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(vertexBuffer);// +primitive->firstVertex * sizeof(vkglTF::Vertex);
					indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(model.indices.buffer) + primitive->firstIndex * sizeof(uint32_t);
					transformBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(transformBuffer.buffer) + nodeIdx++ * sizeof(VkTransformMatrixKHR);

					VkAccelerationStructureGeometryKHR asGeometry{}; // per gltf primitive
					asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
					asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
					asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
					asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
					asGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
					asGeometry.geometry.triangles.maxVertex = primitive->vertexCount + 1;
					asGeometry.geometry.triangles.vertexStride = vertexStride;
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
			//accelerationStructureBuildGeometryInfo.flags = isDeformable ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
			accelerationStructureBuildGeometryInfo.flags = isDeformable ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
				//accelerationStructureBuildGeometryInfo.flags = isDeformable ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
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

void MyBvhTest::initClusterBLASes()
{
	// Use transform matrices from the glTF nodes
	std::vector<VkTransformMatrixKHR> transformMatrices{}; // per node
	for (auto node : model.linearNodes) {
		if (node->mesh)
		{
			for (auto primitive : node->mesh->primitives) {
				if (primitive->indexCount > 0) {
					VkTransformMatrixKHR transformMatrix{};
					//auto m = glm::mat3x4(glm::transpose(node->getMatrix()));
					auto m = glm::mat3x4(1.f);
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

	// custom mesh
	if (openCubeMesh)
	{
		VkBuffer vertexBuffer = openCubeMesh->vertexBuffer.buffer;
		VkBuffer indexBuffer = openCubeMesh->indexBuffer.buffer;
		uint32_t primitiveCount = openCubeMesh->indexBuffer.count / 3;
		VkDeviceSize vertexStride = sizeof(glm::vec3); // position
		PerBLASBuildInfo& refPerBlasBuildInfo = staticPerBlasBuildInfos.emplace_back(PerBLASBuildInfo{});
		std::vector<uint32_t> maxPrimitiveCounts{}; //

		VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{ .deviceAddress = getBufferDeviceAddress(vertexBuffer) };
		VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{ .deviceAddress = getBufferDeviceAddress(indexBuffer) };
		VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{ .deviceAddress = getBufferDeviceAddress(transformBuffer.buffer) };

		VkAccelerationStructureGeometryKHR asGeometry{}; // per gltf primitive
		asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		asGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
		asGeometry.geometry.triangles.maxVertex = openCubeMesh->vertexBuffer.count;
		asGeometry.geometry.triangles.vertexStride = vertexStride;
		asGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
		asGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
		asGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;
		refPerBlasBuildInfo.asGeometries.push_back(asGeometry);
		maxPrimitiveCounts.push_back(primitiveCount);

		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
		buildRangeInfo.firstVertex = 0;
		buildRangeInfo.primitiveOffset = 0;
		buildRangeInfo.primitiveCount = primitiveCount;
		buildRangeInfo.transformOffset = 0;
		refPerBlasBuildInfo.buildRangeInfos.push_back(buildRangeInfo);

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR& accelerationStructureBuildGeometryInfo = refPerBlasBuildInfo.asBuildGeometryInfo;
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
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

		staticBLASes.push_back(blas);
	}


	// clustered blases are always deformable object
	for (uint32_t meshIdx = 0; meshIdx < model.clusteredGeometryNodes.size(); ++meshIdx)
	{
		const ClusteredGeometryNodeRT& refGeometryNode = model.clusteredGeometryNodes[meshIdx];
		const myglTF::ModelRT::PerMeshClustersBuildData& perMeshClusterData = model.perMeshClustersBuildDatas[meshIdx];
		VkDeviceSize vertexStride = sizeof(myglTF::VertexSkinning);

		// per cluster
#if BLAS_PER_CLUSTER
		for (uint32_t i = 0; i < perMeshClusterData.clustersCPU.size(); ++i)
		{
			// empalce back and get reference - for avoiding dangling pointer due to moving array;
			PerBLASBuildInfo& refPerBlasBuildInfo = dynamicPerBlasBuildInfos.emplace_back(PerBLASBuildInfo{});
			std::vector<uint32_t> maxPrimitiveCounts{};
			const ClusterRT& cluster = perMeshClusterData.clustersCPU[i];
#elif GEOMETRY_PER_CLUSTER
		// empalce back and get reference - for avoiding dangling pointer due to moving array;
		PerBLASBuildInfo& refPerBlasBuildInfo = dynamicPerBlasBuildInfos.emplace_back(PerBLASBuildInfo{});
		std::vector<uint32_t> maxPrimitiveCounts{};
		for (uint32_t i = 0; i < perMeshClusterData.clustersCPU.size(); ++i)
		{
			const ClusterRT& cluster = perMeshClusterData.clustersCPU[i];
#endif

			// Build
			// One geometry per glTF node, so we can index materials using gl_GeometryIndexEXT

			VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
			VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
			VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

			vertexBufferDeviceAddress.deviceAddress = refGeometryNode.vertexBufferDeviceAddress; // all scene vertices
			indexBufferDeviceAddress.deviceAddress = refGeometryNode.indexBufferDeviceAddress + (perMeshClusterData.indexStartOffset + cluster.firstTriangle * 3) * sizeof(uint32_t);
			transformBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(transformBuffer.buffer) + meshIdx * sizeof(VkTransformMatrixKHR); // but this mat is identity

			// gl_GeometryIndexEXT
			VkAccelerationStructureGeometryKHR asGeometry{}; // geometry == cluseter
			asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			asGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
			asGeometry.geometry.triangles.maxVertex = cluster.numVertices + 1;
			asGeometry.geometry.triangles.vertexStride = vertexStride;
			asGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
			asGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
			asGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;
			refPerBlasBuildInfo.asGeometries.push_back(asGeometry);

			maxPrimitiveCounts.push_back(cluster.numTriangles);

			VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
			buildRangeInfo.firstVertex = 0;
			buildRangeInfo.primitiveOffset = 0; // primitive->firstIndex * sizeof(uint32_t);
			buildRangeInfo.primitiveCount = cluster.numTriangles;
			buildRangeInfo.transformOffset = 0;
			refPerBlasBuildInfo.buildRangeInfos.push_back(buildRangeInfo);
#if BLAS_PER_CLUSTER
			// Get size info
			VkAccelerationStructureBuildGeometryInfoKHR& accelerationStructureBuildGeometryInfo = refPerBlasBuildInfo.asBuildGeometryInfo;
			accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
#if FORCE_STATIC_SCENE
			accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
#else
			//accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
			accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
			//accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

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
			refPerBlasBuildInfo.blasScratchSizeMax = std::max(accelerationStructureBuildSizesInfo.buildScratchSize, accelerationStructureBuildSizesInfo.updateScratchSize);
			dynamicBLASes.push_back(blas);
		}
	}
#elif GEOMETRY_PER_CLUSTER
		}
		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR& accelerationStructureBuildGeometryInfo = refPerBlasBuildInfo.asBuildGeometryInfo;
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
#if FORCE_STATIC_SCENE
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
#else
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
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
		refPerBlasBuildInfo.blasScratchSizeMax = std::max(accelerationStructureBuildSizesInfo.buildScratchSize, accelerationStructureBuildSizesInfo.updateScratchSize);
		dynamicBLASes.push_back(blas);
	}
#endif

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

void MyBvhTest::initTLAS()
{
	VkTransformMatrixKHR transformMatrix = {
		   1.0f, 0.0f, 0.0f, 0.0f,
		   0.0f, -1.0f, 0.0f, 0.0f,
		   0.0f, 0.0f, 1.0f, 0.0f };

	std::set<uint32_t> offsetSet;
	for (const auto& node : model.clusteredGeometryNodes)
	{
		for (uint32_t i = 0; i < node.numClusters; ++i)
			offsetSet.emplace(node.clusterStartOffset);
	}
	std::vector<uint32_t> clusterStartOffsets(offsetSet.begin(), offsetSet.end());
	clusterStartOffsets.push_back(0xffff);

	uint32_t i = 0;
	uint32_t dynamicBlasCustomIndex = 0;
	for (auto& blas : dynamicBLASes)
	{
		VkAccelerationStructureInstanceKHR blasInstance{};
		blasInstance.transform = transformMatrix;

		if (i >= clusterStartOffsets[dynamicBlasCustomIndex + 1])
		{
			++dynamicBlasCustomIndex;
		}
		++i;
		blasInstance.instanceCustomIndex = dynamicBlasCustomIndex; // dynamicBlasCustomIndex
		blasInstance.mask = 0xFF;
		blasInstance.instanceShaderBindingTableRecordOffset = 0;
		blasInstance.flags = 0;
		blasInstance.accelerationStructureReference = blas.deviceAddress;
		blasInstances.push_back(blasInstance);
	}
	/*std::vector<uint32_t> clusterStartOffsets;

	for (const auto& node : model.clusteredGeometryNodes)
	{
		for (uint32_t i = 0; i < node.numClusters; ++i)
			clusterStartOffsets.push_back(node.clusterStartOffset);
	}
	*/
	for (auto& blas : staticBLASes)
	{
		VkAccelerationStructureInstanceKHR blasInstance{};
		blasInstance.transform = transformMatrix;
		blasInstance.instanceCustomIndex = 0xff;
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

void MyBvhTest::buildBLASes(VkCommandBuffer cmdBuffer)
{
	//CPUTimer timer("Build Blas Timer");

	// TODO build Static-Dynamic BLAS Parallelly
	uint32_t numStaticBlases = staticBLASes.size();
	uint32_t numDynamicBlases = dynamicBLASes.size(); // for Deformable Mesh

	//VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
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
		if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
		vkCmdBuildAccelerationStructuresKHR(
			cmdBuffer,
			numDynamicBlases,
			dynamicBlasBuildingSets.buildGeometryInfos.data(),
			dynamicBlasBuildingSets.buildRangeInfosArray.data());
		if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);


		//if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
		//for (int i = 0; i < numDynamicBlases; ++i)
		//{
		//	vkCmdBuildAccelerationStructuresKHR(
		//		cmdBuffer,
		//		1,
		//		dynamicBlasBuildingSets.buildGeometryInfos.data() + i,
		//		dynamicBlasBuildingSets.buildRangeInfosArray.data() + i);
		//}
		//if (!isFirstBuild) gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
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

void MyBvhTest::hcbBuildBLASes(VkCommandBuffer cmdBuffer)
{
	//CPUTimer timer("Build Blas Timer");
	uint32_t numStaticBlases = staticBLASes.size();
	uint32_t numDynamicBlases = dynamicBLASes.size(); // for Deformable Mesh

	//VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	const bool isFirstBuild = (numStaticBlases && staticBLASes[0].handle == VK_NULL_HANDLE)
		|| (numDynamicBlases && dynamicBLASes[0].handle == VK_NULL_HANDLE);
	//gpuTimer->reset(commandBuffer);
	//gpuTimer->record(commandBuffer, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0);

	static int readyCount = 0;
	// ramda func
	auto processBLASes = [&](auto& blases, auto& buildInfos, auto& buildingSets)
	{
		for (uint32_t blasIdx = 0; blasIdx < buildInfos.size(); ++blasIdx) 
		{
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
				if (model.clusterViewer[blasIdx].triangleHitMask > 0 || readyCount < 4) // TODO Temp : this should be model.clusterViewer[mesh's first cluster offset + cur cluster]
				{
					buildingSets.buildRangeInfosArray.push_back(refBuildInfo.buildRangeInfos.data());
					buildingSets.buildGeometryInfos.push_back(refBuildInfo.asBuildGeometryInfo);
				}
				refBuildInfo.asBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
				refBuildInfo.asBuildGeometryInfo.srcAccelerationStructure = blas.handle;
				refBuildInfo.asBuildGeometryInfo.dstAccelerationStructure = blas.handle;
			}
		}
	};

	// static blas
	processBLASes(staticBLASes, staticPerBlasBuildInfos, staticBlasBuildingSets);
	// dynamic blas
	ASBuildSets tempBuildSets{};
	tempBuildSets.buildRangeInfosArray.reserve(dynamicBlasBuildingSets.buildRangeInfosArray.size());
	tempBuildSets.buildGeometryInfos.reserve(dynamicBlasBuildingSets.buildGeometryInfos.size());
	ASBuildSets& refDynamicBlasBuildSets = isFirstBuild ? dynamicBlasBuildingSets : tempBuildSets;
	processBLASes(dynamicBLASes, dynamicPerBlasBuildInfos, refDynamicBlasBuildSets);
	refDynamicBlasBuildSets.buildRangeInfosArray.shrink_to_fit();
	refDynamicBlasBuildSets.buildGeometryInfos.shrink_to_fit();



	// dynamic blas
	if (numDynamicBlases)
	{
		vkCmdBuildAccelerationStructuresKHR(
			cmdBuffer,
			refDynamicBlasBuildSets.buildGeometryInfos.size(),
			refDynamicBlasBuildSets.buildGeometryInfos.data(),
			refDynamicBlasBuildSets.buildRangeInfosArray.data());
	}
	//gpuTimer->record(commandBuffer, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 1);

	// static blas
	if (numStaticBlases)
	{
		vkCmdBuildAccelerationStructuresKHR(
			cmdBuffer,
			numStaticBlases,
			staticBlasBuildingSets.buildGeometryInfos.data(),
			staticBlasBuildingSets.buildRangeInfosArray.data());

	}
	++readyCount;
}

void MyBvhTest::buildTLAS(VkCommandBuffer cmdBuffer)
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

void MyBvhTest::createShaderBindingTables()
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

void MyBvhTest::createComputePipeline()
{
#if BINDLESS_SKINNING
	animBindlessPass = std::make_unique<MyBindlessAnimComputePass>(device);
	animBindlessPass->createDescriptorSets(model);
	animBindlessPass->createPipeline(getShadersPath() + "myRayTracingLittleAdvanced/animBindless.comp.spv");
#else
	animComputePass = std::make_unique<MyAnimComputePass>(device);
	animComputePass->createDescriptorSets(model);
	animComputePass->createPipeline(getShadersPath() + "myRayTracingLittleAdvanced/anim.comp.spv");
#endif
	

}

void MyBvhTest::createRayTracingPipeline()
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
		// Binding 6: All Clusters SSBO.
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 6),
		// Binding 7: All images used by the glTF model
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 7, imageCount),
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
	VkSpecializationMapEntry* specializationEntries = nullptr;
	// Ray generation group
	{
		shaderStages.push_back(loadShader(getShadersPath() + "myRayTracingLittleAdvanced/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
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
		shaderStages.push_back(loadShader(getShadersPath() + "myRayTracingLittleAdvanced/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		shaderGroups.push_back(shaderGroup);
		// Second shader for shadows
		shaderStages.push_back(loadShader(getShadersPath() + "myRayTracingLittleAdvanced/shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}

	// Closest hit group for doing texture lookups
	{
		VkPipelineShaderStageCreateInfo& shaderStage = shaderStages.emplace_back(loadShader(getShadersPath() + "myRayTracingLittleAdvanced/closesthit_CLUSTER_BLAS1.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));

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
		shaderStages.push_back(loadShader(getShadersPath() + "myRayTracingLittleAdvanced/anyhit.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
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

void MyBvhTest::createDescriptorSets()
{
	uint32_t imageCount = static_cast<uint32_t>(model.textures.size());
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }, // Binding 0: Top level acceleration structure
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, // Binding 1: Ray tracing result image
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, // Binding 2: Uniform data
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // Binding 4: Geometry node information SSBO
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, // Binding 5 : All Mesh Primitives
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, // Binding 6 : Cluster Data
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
		vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &model.primitives.descriptor),
		// Binding 6 : Cluster Data
		vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6, &model.clusters.descriptor)
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
	writeDescriptorImgArray.dstBinding = 7;
	writeDescriptorImgArray.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writeDescriptorImgArray.descriptorCount = imageCount;
	writeDescriptorImgArray.dstSet = rtDescriptorSet;
	writeDescriptorImgArray.pImageInfo = textureDescriptors.data();
	writeDescriptorSets.push_back(writeDescriptorImgArray);

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void MyBvhTest::createUniformBuffer()
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

void MyBvhTest::handleResize()
{
	// Recreate image
	createStorageImage(swapChain.colorFormat, { width, height, 1 });
	// Update descriptor
	VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
	VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(rtDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
	vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
	resized = false;
}

void MyBvhTest::buildCommandBuffers()
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

		gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#if BINDLESS_SKINNING
		animBindlessPass->buildCommandBuffer(cmdBuffer);
#else
		animComputePass->buildCommandBuffer(cmdBuffer);
#endif
		gpuTimer->record(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
		vkCmdPipelineBarrier(
			cmdBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_FLAGS_NONE,
			1, &memBarrier,
			0, nullptr,
			0, nullptr);

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

void MyBvhTest::updateUniformBuffers()
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

void MyBvhTest::getEnabledFeatures()
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

void MyBvhTest::loadAssets()
{
#if BINDLESS_SKINNING
	g_loadingFlag = (myglTF::FileLoadingFlags)(uint64_t)(g_loadingFlag | myglTF::FileLoadingFlags::CombinedMeshBuffer);
#endif

	//model.loadFromFile("D:\\Documents\\Blender\\Exports\\Scene\\DancingScene8.gltf", vulkanDevice, queue, g_loadingFlag);
	//model.loadFromFile("D:\\Documents\\Blender\\Exports\\Models\\Ninja_Dancing0.gltf", vulkanDevice, queue, g_loadingFlag);
	//model.loadFromFile(SCENE_LOCAL_PATH("Scene8"), vulkanDevice, queue, g_loadingFlag);
	//model.loadFromFile(SCENE_LOCAL_PATH("Ninja"), vulkanDevice, graphicsQueue, g_loadingFlag);

	model.loadFromFile("D:\\Documents\\Blender\\Exports\\MocapGuy.gltf", vulkanDevice, graphicsQueue, g_loadingFlag);


			// update all animations, ALl skeletal mesh should have a single animation.
	//for (uint32_t animIdx = 0; animIdx < model.activeAnimations.size(); ++animIdx)
	//{
	//	myglTF::ActiveAnimation& anim = model.activeAnimations[animIdx];
	//	model.updateAnimation(animIdx, 6.35446882);
	//}
	//model.updateJoints();
	//for (auto& node : model.nodes)
	//	model.updateNodeTransforms(node);
}
void MyBvhTest::enableExtensions()
{
	MyVulkanRTBase::enableExtensions();
}

void MyBvhTest::prepare()
{
	MyVulkanRTBase::prepare();
#if MEASURE_MODE
#if defined(_WIN32)
	setupConsole("Vulkan example");
#endif
#endif
#if _DEBUG & !SKIP_SHADER_COMIPLE  // compile shaders
	std::string batchPath = getShadersPath() + "myRayTracingLittleAdvanced/ShaderCompile.bat";
	system(batchPath.c_str());
	std::cout << "\t...current project's shaders compile completed.\n";
#endif
	loadAssets();

	// after asset loaded, create cube, considering min/max
	openCubeMesh = std::make_unique<OpenCubeMesh>();
	openCubeMesh->init(model.sceneBBox.min, model.sceneBBox.max, vulkanDevice, graphicsQueue);
	glm::vec3 cubeCenter = (openCubeMesh->worldMin + openCubeMesh->worldMax) * 0.5f;
	specializationData.lightPos.x = cubeCenter.x;
	//specializationData.lightPos.y = -(openCubeMesh->worldMax.y - 1e-5f);
	specializationData.lightPos.y = -(openCubeMesh->worldMax.y - 0.001);
	specializationData.lightPos.z = -cubeCenter.z;

	pushConstantData.indexBufferDeviceAddress = getBufferDeviceAddress(model.indices.buffer);
	pushConstantData.vertexBufferDeviceAddress = getBufferDeviceAddress(model.vertices.buffer);
	pushConstantData.cubeColor = openCubeMesh->color;

	fixedBlasNum = model.m_numTotalClusters;

	// make timer
	gpuTimer = std::make_unique<GPUTimer>(device, deviceProperties.limits.timestampPeriod, (4/*fixedBlasNum*/) * 2);
	gpuTimer->init();
	//createComputePipeline();

	// Create the acceleration structures used to render the ray traced scene
	VkCommandBuffer accelBuildCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	initClusterBLASes();
	//initTriangleBLASes();
	initTLAS();

	// Calc Acceleration Structure Size
	for (const auto& info : dynamicPerBlasBuildInfos)
		totalBlasSize += info.asSize;

	std::cout << "Acceleration Sturcture Info\n";
	std::cout << "Num Blases : " << dynamicBLASes.size() << "\n";
	std::cout << "BLASes Size : " << totalBlasSize / 1024.0 << " KB\n";
	std::cout << "TLAS Size : " << tlasSize / 1024.0 << " KB\n";
	std::cout << "Total AS Size : " << (totalBlasSize + tlasSize) / 1024.0 << " KB\n";
	std::cout << "=================================\n";


	//hcbBuildBLASes(accelBuildCmdBuffer);
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

void MyBvhTest::draw()
{
	MyVulkanBase::prepareFrame();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	MyVulkanBase::submitFrame();
}

void MyBvhTest::render()
{
	if (!prepared)
		return;

#if !FORCE_STATIC_SCENE
	if (!paused)
	{
		// Update Animation
		static float accTime = 0.f; // accumulated Time
		static float animationSpeed = 1.f;
			
		// update all animations, ALl skeletal mesh should have a single animation.
		for (uint32_t animIdx = 0; animIdx < model.activeAnimations.size(); ++animIdx)
		{
			myglTF::ActiveAnimation& anim = model.activeAnimations[animIdx];
			anim.accPlayTime += frameTimer;
			if (anim.accPlayTime > anim.end)
				anim.accPlayTime = 0.f;
			//myUtils::CPUTimer timer(true);
			//model.updateAnimation(animIdx, animationSpeed * anim.accPlayTime);
			//timer.record(true);
		}
		model.updateJoints();
		for (auto& node : model.nodes)
			model.updateNodeTransforms(node);
#if BINDLESS_SKINNING
		model.updateCombinedMeshBuffer();
#endif
	}
#endif

	updateUniformBuffers();
	uniformData.frame = -1;

	model.updateClustersAABB(graphicsQueue);
	draw();
#if MEASURE_MODE
	static uint32_t frameCount, accFPS = 0;
	static float accBuildBLASTime, accBuildTLASTime, accAnimTime = 0.f;
	static float accTraceTime = 0.f;

	++frameCount;

	/*static bool blasInfoViewerInited = false;
	int totalClusters = model.m_numTotalClusters;
	static std::vector<float> blasBuildTimes;
	if (!blasInfoViewerInited)
		blasBuildTimes.resize(totalClusters, 0.f);
	blasInfoViewerInited = true;*/

	//static float blasBuildTimes[fixedBlasNum];
	if (frameCount >= WARMINGUP_FRAME && frameCount <= MEASURE_END_FRAME)
	{
		accFPS += lastFPS;
		const std::vector<float> gpuTimerResult = gpuTimer->timerResult();
		accAnimTime		 += gpuTimerResult[0];
		accBuildBLASTime += gpuTimerResult[1];
		accBuildTLASTime += gpuTimerResult[2];
		accTraceTime	 += gpuTimerResult[3];

		/*for (int i = 0; i < totalClusters; ++i)
		{
			blasBuildTimes[i] += gpuTimerResult[i];
		}*/

		if (frameCount == MEASURE_END_FRAME)
		{
			// [<<numTri, numVert>, buildTime>]
			//std::vector<std::pair<std::pair<uint32_t, uint32_t>, float>> clusterBlasBuildInfos; // <<numTri, numVert>, buildTime>
			//for (int i = 0; i < totalClusters; ++i)
			//{
			//	const auto& cluster = model.tempClusters[i];
			//	clusterBlasBuildInfos.emplace_back(std::make_pair(std::make_pair(cluster.numTriangles, cluster.numVertices), blasBuildTimes[i]));
			//}
			//std::sort(clusterBlasBuildInfos.begin(), clusterBlasBuildInfos.end(),
			//	[](const std::pair<std::pair<uint32_t, uint32_t>, float>& a, std::pair<std::pair<uint32_t, uint32_t>, float>& b)
			//	{
			//		return a.second < b.second;

			//		if (a.first.first != b.first.first)
			//			return a.first.first < b.first.first;
			//		return a.first.second < b.first.second;
			//	});
			//for (const auto& pair : clusterBlasBuildInfos)
			//{
			//	std::cout << "[" << pair.first.first << ", " << pair.first.second << ", " << pair.second / MEASURE_FRAME_COUNT << "]\n";
			//}

			//std::sort(blasBuildTimes.begin(), blasBuildTimes.end());
			//for (int i = 0; i < totalClusters; ++i)
			//{
			//	std::cout << blasBuildTimes[i] / MEASURE_FRAME_COUNT << ", ";
			//}

			float blasAvg = accBuildBLASTime / MEASURE_FRAME_COUNT;
			float tlasAvg = accBuildTLASTime / MEASURE_FRAME_COUNT;
			float animAvg = accAnimTime / MEASURE_FRAME_COUNT;
			float fpsAvg = (float)accFPS / MEASURE_FRAME_COUNT;
			printf("With Cluster BLAS, Measured Frame Count: %d\n", MEASURE_FRAME_COUNT);
			printf("Average Animation Time = %.3f (ms)\n", animAvg);
			printf("Average BLAS Build Time = %.3f (ms)\n", blasAvg);
			printf("Average TLAS Build Time = %.3f (ms)\n", tlasAvg);
			printf("Average Total AS Build Time = %.3f (ms)\n", blasAvg + tlasAvg);
			printf("Average Tracing Time = %.3f (ms)\n", accTraceTime / MEASURE_FRAME_COUNT);
			printf("Average AS + Ray Time = %.3f (ms)\n", blasAvg + tlasAvg + accTraceTime / MEASURE_FRAME_COUNT);
			printf("Average FPS = %.3f fps (%.3f ms)\n", fpsAvg, 1000.f / fpsAvg);
			printf("=================================\n");
			printf("Excel\n");
			printf("%.1f\n%.1f\n%.3f\n%.3f\n%.3f\n%.3f\n%.3f\n%.3f\n",
				totalBlasSize / 1024.0,
				(totalBlasSize + tlasSize) / 1024.0,
				blasAvg,
				blasAvg + tlasAvg,
				accTraceTime / MEASURE_FRAME_COUNT,
				blasAvg + tlasAvg + (accTraceTime / MEASURE_FRAME_COUNT),
				fpsAvg,
				1000.f / fpsAvg
			);
			printf("=================================\n");
		}
		//model.updateGeometryNode(blasBuildTimes.data(), totalClusters);
	}
#endif

}

void MyBvhTest::OnUpdateUIOverlay(vks::UIOverlay* overlay)
{
	if (overlay->header("Visibility"))
	{
		(overlay->radioButton("Render Texture", (int*)&pushConstantData.renderMode, 0));
		(overlay->radioButton("Render Triangle", (int*)&pushConstantData.renderMode, 1));
		(overlay->radioButton("Render Cluster", (int*)&pushConstantData.renderMode, 2));
	}
}
