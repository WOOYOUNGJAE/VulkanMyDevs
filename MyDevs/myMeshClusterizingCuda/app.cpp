#include "app.h"

/**
 * 0==myMeshClusterizingCuda
 * 1==myMeshClusterizingMeshOpt
 */
#define RT_PROJECT_VERSION 01

#if (RT_PROJECT_VERSION == 0)
#include "myMeshClusterizingCuda.h"
#define RT_CLASS MyMeshClusterizingCuda
#elif (RT_PROJECT_VERSION == 1)
#include "myMeshClustrizingMeshopt.h"
#define RT_CLASS MyMeshClustrizingMeshopt
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