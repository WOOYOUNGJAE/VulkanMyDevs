#version 460

#define JOINT_RENDER 0
#include "shaderInclude.glsl"
// Specialization Constant
//layout(constant_id = 0) const uint32_t NUM_STATIC_INSTANCE = 0;
const vec3 POINT_LIGHT_COLOR = vec3(0.9f, 0.95f, 1.f);
layout(constant_id = 0) const float POINT_LIGHT0_POS_X = 0.f;
layout(constant_id = 1) const float POINT_LIGHT0_POS_Y = -2.2f;
layout(constant_id = 2) const float POINT_LIGHT0_POS_Z = 0.f;


layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	uint primitiveID = gl_PrimitiveID;
	uint geometryID = gl_GeometryIndexEXT;
//	uint clusterID = gl_GeometryIndexEXT; // or gl_InstanceID;
	uint instanceID = gl_InstanceID;
	uint customID = gl_InstanceCustomIndexEXT; // Instanced from Which BLAS?


	if (pushData.baseData.renderMode == 1)
	{
		uint h = primitiveID * 1664525u + 1013904223u;
		hitValue = vec3(
			float((h >>  0) & 0xFF),
			float((h >>  8) & 0xFF),
			float((h >> 16) & 0xFF)
		) / 255.0 * 0.3 + 0.5;
		return;
	}
	else if (pushData.baseData.renderMode == 2)
	{
		uint h = instanceID * 1664525u + 1013904223u;
		hitValue = vec3(
			float((h >>  0) & 0xFF),
			float((h >>  8) & 0xFF),
			float((h >> 16) & 0xFF)
		) / 255.0 * 0.3 + 0.5;
		return;
	}

	vec3 epsilonDir;
	vec3 worldRayDir = gl_WorldRayDirectionEXT;
	if (customID == 0xff) // cube (or special static mesh)
	{
		hitValue = pushData.cubeColor.rgb;;
		epsilonDir = -worldRayDir;
	}
	else
	{
		MeshPrimitive meshPrimitive;
#if CLUSTER_BLAS
		GeometryNode geometryNode = geometryNodes.nodes[customID];
		ClusterRT cluster = sceneClusters.clusters[instanceID];
		Triangle tri = unpackTriangle(geometryNode, cluster, primitiveID);
		meshPrimitive = meshPrimitives.primitives[geometryNode.primitiveStartOffset + tri.vertices[0].customData4.y];
#else
		GeometryNode geometryNode = geometryNodes.nodes[instanceID];
		meshPrimitive = meshPrimitives.primitives[geometryNode.primitiveStartOffset + geometryID];
		Triangle tri = unpackTriangle(geometryNode, meshPrimitive, primitiveID);
#endif

		epsilonDir = tri.normal;
		if (nonuniformEXT(meshPrimitive.textureIndexBaseColor) == -1)
		{
			hitValue = vec3(1,1,1);
		}
		else
		{
			vec3 color = texture(textures[nonuniformEXT(meshPrimitive.textureIndexBaseColor)], tri.uv).rgb;
			if (meshPrimitive.textureIndexOcclusion > -1) {
				float occlusion = texture(textures[nonuniformEXT(meshPrimitive.textureIndexOcclusion)], tri.uv).r;
				color *= occlusion;
			}
			hitValue = color;
		}
	}	


	// Shadow casting
	float epsilon = 0.01;
	vec3 origin = gl_WorldRayOriginEXT + worldRayDir * gl_HitTEXT + epsilonDir * epsilon;
	vec3 lightPos = vec3(POINT_LIGHT0_POS_X, POINT_LIGHT0_POS_Y, POINT_LIGHT0_POS_Z);
	vec3 lightVector = (lightPos - origin);
	float tmax = length(lightVector);
	lightVector = normalize(lightVector);
	float tmin = 0.001;

	// Trace shadow ray and offset indices to match shadow hit/miss shader group indices
	shadowed = true;	
	traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, tmin, lightVector, tmax, 2);
	if (shadowed) {
		hitValue *= 0.7;
	}
	else
		hitValue *= POINT_LIGHT_COLOR;
}
