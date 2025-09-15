#include "myHitCountBasedBlasBuilding.h"

/**
 * 0== Triangle BLAS
 * 1== Clustered BLAS
 */
#define RT_PROJECT_VERSION 0

#if (RT_PROJECT_VERSION == 0)
#include "myHCBTriangle.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::ClusteredTriangleBLAS);
#define RT_CLASS MyHCBTriangle
#elif (RT_PROJECT_VERSION == 1)
#include "myHCBCluster.h"
myglTF::FileLoadingFlags g_loadingFlag = myglTF::FileLoadingFlags(myglTF::FileLoadingFlags::MakeClusters);
#define RT_CLASS MyHCBCluster
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