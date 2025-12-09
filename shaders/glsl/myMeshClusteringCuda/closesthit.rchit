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

	uint h = instanceID * 1664525u + 1013904223u;
	hitValue = vec3(
		float((h >>  0) & 0xFF),
		float((h >>  8) & 0xFF),
		float((h >> 16) & 0xFF)
	) / 255.0 * 0.3 + 0.5;

	// hitValue = vec3(1,0,1);

	return;
}
