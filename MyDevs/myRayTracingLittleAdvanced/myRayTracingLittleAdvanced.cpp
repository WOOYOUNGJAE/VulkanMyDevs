#include "myRayTracingLittleAdvanced.h"

/**
 * 0==multiblas
 * 1==dynamicAS
 * 2==buildIndirect
 */
#define RT_PROJECT_VERSION 1

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
#endif


#define RUN_RT_EXAMPLE(ClassName) \
ClassName* runInstance; \
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) \
{ \
   if (runInstance != NULL) \
   { \
       runInstance->handleMessages(hWnd, uMsg, wParam, lParam); \
   } \
   return (DefWindowProc(hWnd, uMsg, wParam, lParam)); \
} \
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int) \
{ \
   for (int32_t i = 0; i < __argc; i++) { ClassName::args.push_back(__argv[i]); }; \
   runInstance = new ClassName(); \
   runInstance->initVulkan(); \
   runInstance->setupWindow(hInstance, WndProc); \
   runInstance->prepare(); \
   runInstance->renderLoop(); \
   delete(runInstance); \
   return 0; \
}

// Usage of the macro
RUN_RT_EXAMPLE(RT_CLASS);



//MyBuildASIndirect* runInstance;
//
//LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
//{
//    if (runInstance != NULL)
//    {
//        runInstance->handleMessages(hWnd, uMsg, wParam, lParam);
//    }
//    return DefWindowProc(hWnd, uMsg, wParam, lParam);
//}
//
//int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int)
//{
//    for (int32_t i = 0; i < __argc; i++) {
//        MyBuildASIndirect::args.push_back(__argv[i]);
//    }
//
//    runInstance = new MyBuildASIndirect();
//    runInstance->initVulkan();
//    runInstance->setupWindow(hInstance, WndProc);
//    runInstance->prepare();
//    runInstance->renderLoop();
//    delete runInstance;
//
//    return 0;
//}