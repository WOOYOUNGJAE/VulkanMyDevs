// Includes for both CPU, GPU
#ifndef MY_INCLUDES_CPU_GPU
#define MY_INCLUDES_CPU_GPU

#define WAVE_SIZE 32 // warp sizes

#ifdef __cplusplus
typedef unsigned int uint;
#endif

struct Payload_MeshShader
{
	uint meshletIndices[WAVE_SIZE];
};

#endif