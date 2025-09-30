/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
#version 460
#define JOINT_RENDER 0
#include "shaderInclude.glsl"
layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{

	Triangle tri = unpackTriangle(gl_PrimitiveID);

#if JOINT_RENDER
	const float f = pushData.jointWeightRenderThreshold;
	if (tri.weight0.x > f || tri.weight0.y > f || tri.weight0.z > f || tri.weight0.w > f)
	{
		hitValue = vec3(1,0,1);return;
	}
	else
	{
		hitValue = vec3(1,1,0);return;
	}
#endif

	uint primitiveID = gl_PrimitiveID;
	uint clusterID = gl_GeometryIndexEXT; // or gl_InstanceID;
	uint instanceID = gl_InstanceID;

//	if (pushData.baseData.renderMode == 1)
	{

		uint h = instanceID * 1664525u + 1013904223u;
		hitValue = vec3(
			float((h >>  0) & 0xFF),
			float((h >>  8) & 0xFF),
			float((h >> 16) & 0xFF)
		) / 255.0 * 0.3 + 0.5;
		return;
	}
	


	GeometryNode geometryNode = geometryNodes.nodes[gl_InstanceID];
//	MeshPrimitive meshPrimitive = meshPrimitives.primitives[1];
	MeshPrimitive meshPrimitive = meshPrimitives.primitives[geometryNode.primitiveStartOffset + gl_GeometryIndexEXT];
		
//	hitValue = vec3(float(meshPrimitive.textureIndexBaseColor) / 100.f);return;
	if (nonuniformEXT(meshPrimitive.textureIndexBaseColor) == -1)
	{
		hitValue = vec3(1,1,0);return;
	}

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
