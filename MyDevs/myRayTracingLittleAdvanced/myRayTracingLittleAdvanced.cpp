#include "myRayTracingLittleAdvanced.h"

/**
 * 0==multiblas
 * 1==dynamicAS - TODO not work now. use skeletalAnimationRT
 * 2==buildIndirect - 
 * 3==skeletalAnimationRT
 * 4==bakedAnimationRT
 */
#define RT_PROJECT_VERSION 5

#if (RT_PROJECT_VERSION == 0)
#include "myMultiBLAS.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::GeometryNodePerMesh);
#define RT_CLASS MyMultiBLAS
#elif (RT_PROJECT_VERSION == 1)
#include "myDynamicAccelerationStructure.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::GeometryNodePerMesh);
#define RT_CLASS MyDynamicAccelerationStructure
#elif (RT_PROJECT_VERSION == 2)
#include "myBuildASIndirect.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::GeometryNodePerMesh);
#define RT_CLASS MyBuildASIndirect
#elif (RT_PROJECT_VERSION == 3)
#include "mySkeletalAnimationRT.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::GeometryNodePerMesh);
#define RT_CLASS MySkeletalAnimationRT
#elif (RT_PROJECT_VERSION == 4)
#include "myBakedAnimationRT.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::GeometryNodePerMesh);
#define RT_CLASS MyBakedAnimationRT
#elif (RT_PROJECT_VERSION == 5)
#include "myBvhTest.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::ClusteredTriangleBLAS);
//myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::GeometryNodePerMesh);
#define RT_CLASS MyBvhTest
#endif

RT_CLASS* runInstance;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (runInstance != NULL)
    {
        runInstance->handleMessages(hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int)
{
    for (int32_t i = 0; i < __argc; i++) {
        RT_CLASS::args.push_back(__argv[i]);
    }

    runInstance = new RT_CLASS();
    runInstance->initVulkan();
    runInstance->setupWindow(hInstance, WndProc);
    runInstance->prepare();
    runInstance->renderLoop();
    delete runInstance;

    return 0;
}