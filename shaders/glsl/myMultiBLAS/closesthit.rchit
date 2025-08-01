/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 2) rayPayloadEXT bool shadowed;
hitAttributeEXT vec2 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 3, set = 0) uniform sampler2D image;

struct GeometryNode {
	uint32_t vertexStartOffset; // from scene's total vertex buffer
	uint32_t indexStartOffset; // from scene's total Index buffer
	// primitive contains material info
	uint32_t primitiveStartOffset;
};
layout(binding = 4, set = 0) buffer GeometryNodes { GeometryNode nodes[]; } geometryNodes;


struct MeshPrimitive
{
	uint32_t vertexStartOffsetInMesh;
	uint32_t IndexStartOffsetInMesh;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};
layout(binding = 5, set = 0) buffer MeshPrimitives { MeshPrimitive primitives[]; } meshPrimitives;
layout(binding = 6, set = 0) uniform sampler2D textures[];

#include "bufferreferences.glsl"
#include "geometrytypes.glsl"

void main()
{
	Triangle tri = unpackTriangle(gl_PrimitiveID, 64);
	hitValue = vec3(tri.normal);

	GeometryNode geometryNode = geometryNodes.nodes[gl_InstanceID];
	MeshPrimitive meshPrimitive = meshPrimitives.primitives[geometryNode.primitiveStartOffset + gl_GeometryIndexEXT];

	vec3 color = texture(textures[nonuniformEXT(meshPrimitive.textureIndexBaseColor)], tri.uv).rgb;
	if (meshPrimitive.textureIndexOcclusion > -1) {
		float occlusion = texture(textures[nonuniformEXT(meshPrimitive.textureIndexOcclusion)], tri.uv).r;
		color *= occlusion;
	}

	hitValue = color;

	// Shadow casting
	float tmin = 0.001;
	float tmax = 10000.0;
	float epsilon = 0.001;
	vec3 origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + tri.normal * epsilon;
	shadowed = true;  
	vec3 lightVector = vec3(-5.0, -2.5, -5.0);
	// Trace shadow ray and offset indices to match shadow hit/miss shader group indices
//	traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, tmin, lightVector, tmax, 2);
//	if (shadowed) {
//		hitValue *= 0.7;
//	}
}
void FOO()
{
	// Get GeometryNode(Mesh's) via hit BLAS instance (gl_InstanceID)
	GeometryNode geometryNode = sceneNodes[gl_InstanceID];
	// gl_GeometryIndexEXT represents current gltf primitive from mesh
	Primitive primitive = scenePrimitives[geometryNode.primitiveStartOffset + gl_GeometryIndexEXT];
	
	uint64_t vertexAddress = sceneDeviceAddress.vertexBufferAddress; // vertex buffer is combinded single buffer
	uint64_t triangleIndexOffsetInBytes = INDEX_TYPE_SIZE * (geometryNode.indexStartOffset + meshPrimitive.IndexStartOffsetInMesh + (gl_PrimitiveID * 3));
	uint64_t currentTriangleAddress = sceneDeviceAddress.indexBufferAddress + triangleIndexOffsetInBytes;

	Vertices   vertices = Vertices(vertexAddress);
	Indices    indices = Indices(currentTriangleAddress);	
}

