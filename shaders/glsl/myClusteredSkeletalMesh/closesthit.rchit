#version 460

#include "shaderInclude.glsl"
layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	uint primitiveID = gl_PrimitiveID;
	uint clusterID = gl_ClusterIDNV_;
	uint instanceID = gl_InstanceID;
	if (pushData.renderMode > 0)
	{
		uint minUnitID = (pushData.renderMode == 1) ? primitiveID : clusterID;
	
		uint h = minUnitID * 1664525u + 1013904223u;
		hitValue = vec3(
			float((h >>  0) & 0xFF),
			float((h >>  8) & 0xFF),
			float((h >> 16) & 0xFF)
		) / 255.0 * 0.3 + 0.5;
		return;
	}

	Triangle tri = unpackTriangle(instanceID, clusterID, primitiveID);
	
	GeometryNode geometryNode = geometryNodes.nodes[instanceID];
	ClusterRT cluster = sceneClusters.clusters[geometryNode.clusterStartOffset + clusterID];


	// offset from all scene's triangles
	uint triOffsetGlobal = geometryNode.triangleStartOffset + cluster.firstTriangle + primitiveID;


	// find primitive Index
	uint primitiveIdx = 0;
	for (uint i = 0; i < pushData.numPrimitives; ++i)
	{
		if (triOffsetGlobal >= meshPrimitives.primitives[i].triangleStartOffsetGlobal)
			primitiveIdx = i;
		else
			break;
	}

	ClusteredMeshPrimitive meshPrimitive = meshPrimitives.primitives[primitiveIdx];


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
