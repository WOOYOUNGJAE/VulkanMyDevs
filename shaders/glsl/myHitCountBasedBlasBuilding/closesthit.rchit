/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
#version 460

#include "shaderInclude.glsl"
layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	uint primitiveID = gl_PrimitiveID;
	uint clusterID = gl_GeometryIndexEXT;
	uint instanceID = gl_InstanceID;
//	if (pushData.renderMode > 0)
	{
		uint minUnitID = (pushData.renderMode == 1) ? primitiveID : clusterID;
		uint h = minUnitID * 1664525u + 1013904223u;
		hitValue = vec3(
			float((h >>  0) & 0xFF),
			float((h >>  8) & 0xFF),
			float((h >> 16) & 0xFF)
		) / 255.0 * 0.3 + 0.5;
	}
	

	Triangle tri = unpackTriangle(instanceID, clusterID, primitiveID);

	GeometryNode geometryNode = geometryNodes.nodes[instanceID];
	sceneClusters.clusters[geometryNode.clusterStartOffset + clusterID].triangleHitMask |= (1 << primitiveID);

////	MeshPrimitive meshPrimitive = meshPrimitives.primitives[1];
//	MeshPrimitive meshPrimitive = meshPrimitives.primitives[geometryNode.primitiveStartOffset + gl_GeometryIndexEXT];
//		
////	hitValue = vec3(float(meshPrimitive.textureIndexBaseColor) / 100.f);return;
//	if (nonuniformEXT(meshPrimitive.textureIndexBaseColor) == -1)
//	{
//		hitValue = vec3(1,1,0);return;
//	}
//
//	vec3 color = texture(textures[nonuniformEXT(meshPrimitive.textureIndexBaseColor)], tri.uv).rgb;
//	if (meshPrimitive.textureIndexOcclusion > -1) {
//		float occlusion = texture(textures[nonuniformEXT(meshPrimitive.textureIndexOcclusion)], tri.uv).r;
//		color *= occlusion;
//	}
//
//	hitValue = color;
//
//	// Shadow casting
//	float tmin = 0.001;
//	float tmax = 10000.0;
//	float epsilon = 0.001;
//	vec3 origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + tri.normal * epsilon;
//	shadowed = true;  
//	vec3 lightVector = vec3(-5.0, -2.5, -5.0);
//	// Trace shadow ray and offset indices to match shadow hit/miss shader group indices
////	traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, tmin, lightVector, tmax, 2);
////	if (shadowed) {
////		hitValue *= 0.7;
////	}
}
