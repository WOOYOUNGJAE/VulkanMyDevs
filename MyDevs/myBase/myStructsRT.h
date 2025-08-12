#pragma once

#include "myIncludesCPUGPU.h"


// Original Sashca Style
struct GeometryNodePerPrimitiveRT
{
	uint64_t vertexBufferDeviceAddress;
	uint64_t indexBufferDeviceAddress; 
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};


struct GeometryNodePerMeshRT
{
	uint32_t vertexStartOffset; // from scene's total vertex buffer
	uint32_t indexStartOffset; // from scene's total Index buffer
	// primitive contains material info
	// Access like AllPrimitives[primitiveStartOffset + gl_GeometryIndexEXT]
	uint32_t primitiveStartOffset;
};

struct MeshPrimitive
{
	uint32_t vertexStartOffsetInMesh;
	uint32_t IndexStartOffsetInMesh;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};

// Extended version of GeometryNodeRT for CLAS
typedef ClusteredGeometryData ClusteredGeometryNodeRT;

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