
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#extension GL_NV_cluster_acceleration_structure : require
#extension GL_EXT_spirv_intrinsics : require
spirv_decorate(extensions = ["SPV_NV_cluster_acceleration_structure"], capabilities = [5437], 11, 5436) in int gl_ClusterIDNV_;

layout(location = 2) rayPayloadEXT bool shadowed;

hitAttributeEXT vec2 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 3, set = 0) uniform sampler2D image;

#include "../../../MyDevs/myBase/myIncludesCPUGPU.h"
#define GeometryNode ClusteredGeometryData
//struct GeometryNode {
//	uint32_t vertexStartOffset; // from scene's total vertex buffer
//	uint32_t indexStartOffset; // from scene's total Index buffer
//	// primitive contains material info
//	uint32_t primitiveStartOffset;
//};
layout(binding = 4, set = 0) buffer GeometryNodes { GeometryNode nodes[]; } geometryNodes;


struct MeshPrimitive
{
	uint32_t vertexStartOffsetInMesh;
	uint32_t IndexStartOffsetInMesh;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};
layout(binding = 5, set = 0) buffer MeshPrimitives { ClusteredMeshPrimitive primitives[]; } meshPrimitives;
layout(binding = 6, set = 0) buffer Clusters { ClusterRT clusters[]; } sceneClusters;
layout(binding = 7, set = 0) uniform sampler2D textures[];

layout(push_constant) uniform pushConstant {
	MainRendererPushConstantData pushData;
};
#define USE_SKINNING
#include "bufferreferences.glsl"
#include "geometrytypes.glsl"