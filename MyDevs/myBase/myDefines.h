#pragma once
// Includes
#include "myVulkan.h"

// Macros
#define MEASURE_MODE 0
/**
 * 0 : simple mailbox
 * 1 : + CPU work starts right after submit draw command
 * 2 : + MultiThread Rendering (TBD)
 */
#define ASYNC_RENDER_LEVEL 0