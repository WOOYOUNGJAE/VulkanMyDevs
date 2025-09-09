/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
#version 460

#include "shaderInclude.glsl"

void main()
{
	if (pushData.bRenderTriangle != 0)
	{
		uint triID = gl_PrimitiveID ;

		uint h = triID * 1664525u + 1013904223u;
		hitValue = vec3(
			float((h >>  0) & 0xFF),
			float((h >>  8) & 0xFF),
			float((h >> 16) & 0xFF)
		) / 255.0 * 0.3 + 0.5;
		return;
	}
	

	Triangle tri = unpackTriangle(gl_PrimitiveID);
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
