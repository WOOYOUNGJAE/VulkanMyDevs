#pragma once

#ifdef __cplusplus
#include <glm/glm.hpp>
#endif

struct GeometryNodeRT
{
	uint64_t vertexBufferDeviceAddress;
	uint64_t indexBufferDeviceAddress;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};

struct ClusterRT
{
	uint16_t numVertices;
	uint16_t numTriangles;
	uint32_t firstTriangle;
	uint32_t firstLocalVertex;
	uint32_t firstLocalTriangle;
};

struct BBox
{
	glm::vec3 min;
	glm::vec3 max;
};