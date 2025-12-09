
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout(location = 2) rayPayloadEXT bool shadowed;

hitAttributeEXT vec2 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

#include "../../../MyDevs/myBase/myIncludesCPUGPU.h"

#if CLUSTER_BLAS
#define GeometryNode ClusteredGeometryData
#else
struct GeometryNode 
{
	uint32_t vertexStartOffset; // from scene's total vertex buffer
	uint32_t indexStartOffset; // from scene's total Index buffer
	// primitive contains material info
	uint32_t primitiveStartOffset;
	uint32_t padding0;
};
#endif

#if CLUSTER_BLAS
#define MeshPrimitive ClusteredMeshPrimitive
#else
struct MeshPrimitive
{
	uint32_t vertexStartOffsetInMesh;
	uint32_t IndexStartOffsetInMesh;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};
#endif

struct PushConstantData
{
	MainRendererPushConstantData baseData;
#if JOINT_RENDER
	float jointWeightRenderThreshold;
#else
	float customData;
#endif
	vec4 cubeColor;
};

layout(push_constant) uniform pushConstant {
	PushConstantData pushData;
};