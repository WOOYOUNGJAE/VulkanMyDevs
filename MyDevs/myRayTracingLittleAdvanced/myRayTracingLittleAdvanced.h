#pragma once
#include "myVulkan.h"
#include "myVulkanRTBase.h"
#include "myglTFModel.h"

#define VK_GLTF_MATERIAL_IDS
#include "myglTFModel.h"


#define SCENE_LOCAL_PATH(NAME) "D:\\Documents\\Blender\\Exports\\Scene\\" NAME ".gltf"


extern myglTF::FileLoadingFlags g_loadingFlag;