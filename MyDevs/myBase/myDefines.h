#pragma once
// Includes
#include "myVulkan.h"

// Macros
#define MEASURE_MODE 1
/**
 * 0 : simple mailbox
 * 1 : + CPU work starts right after submit draw command
 * 2 : + MultiThread Rendering (TBD)
 */
#define ASYNC_RENDER_LEVEL 0
//#pragma comment(lib, "gmcCuda.lib")

#if defined(_DEBUG)
#pragma comment(lib, "D:/Git/Myprojects/VulkanMyDevs/build/lib/x64/Debug/gmcCuda.lib")
#else
#pragma comment(lib, "D:/Git/Myprojects/VulkanMyDevs/build/lib/x64/Release/gmcCuda.lib")
#endif


// Binding Index (register)
#define BINDING_MODEL_DEFAULT 0