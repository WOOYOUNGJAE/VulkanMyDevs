#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;
layout (location = 4) in vec4 inTangent;
#ifdef USE_SKINNING
layout (location = 5) in vec4 inJoint;
layout (location = 6) in vec4 inWeight;
#endif

layout (set = 0, binding = 0) uniform UBOScene 
{
	mat4 projection;
	mat4 view;
	vec4 lightPos;
	vec4 viewPos;
} uboScene;

layout (set = 1, binding = 0) uniform UBOModel
{
	mat4 matrix;
#ifdef USE_SKINNING
	mat4 jointMatrix[64];
	float jointcount;
#endif
} uboModel;


layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;
layout (location = 5) out vec4 outTangent;

void main() 
{
	outNormal = inNormal;
	outColor = inColor;
	outUV = inUV;
	outTangent = inTangent;
	gl_Position = uboScene.projection * uboScene.view * uboModel.matrix * vec4(inPos.xyz, 1.0);
	
	outNormal = mat3(uboModel.matrix) * inNormal;
	vec4 pos = uboModel.matrix * vec4(inPos, 1.0);
	outLightVec = uboScene.lightPos.xyz - pos.xyz;
	outViewVec = uboScene.viewPos.xyz - pos.xyz;
}