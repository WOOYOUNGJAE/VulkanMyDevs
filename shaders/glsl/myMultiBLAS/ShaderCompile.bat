@echo off
cd /d %~dp0
setlocal enabledelayedexpansion

:: Check if VK_SDK_PATH is set
if "%VK_SDK_PATH%"=="" (
    echo Error: VK_SDK_PATH environment variable is not set.
    pause
    exit /b 1
)

:: Get Vulkan version from VK_SDK_PATH
:: Extract version number from the path (e.g., 1.3.283.0)
set "VULKAN_VERSION="
for %%F in ("%VK_SDK_PATH%") do (
    set "VULKAN_VERSION=%%~nxF"
)

if "!VULKAN_VERSION!"=="" (
    echo Error: Could not determine Vulkan version from VK_SDK_PATH.
    pause
    exit /b 1
)

:: Construct the target environment (e.g., vulkan1.3)
set "TARGET_ENV=vulkan!VULKAN_VERSION:~0,3!"

:: Path to glslc.exe
set "GLSLC=%VK_SDK_PATH%\Bin\glslc.exe"

:: Check if glslc.exe exists
if not exist "!GLSLC!" (
    echo Error: glslc.exe not found at %GLSLC%.
    pause
    exit /b 1
)

:: Compile all shader files in the current directory
set "SHADER_EXTENSIONS=.rgen .rmiss .rchit .rahit .vert .frag .comp .geom .tesc .tese .task .mesh"
set "COMPILED_FILES=0"

for %%F in (*) do (
    :: Get the file extension
    set "EXT=%%~xF"
    :: Check if the file extension is in the list of shader extensions
    for %%E in (%SHADER_EXTENSIONS%) do (
        if /i "!EXT!"=="%%E" (
            echo Compiling %%F to %%~nxF.spv...
            "!GLSLC!" --target-env=!TARGET_ENV! --target-spv=spv1.4 "%%F" -o "%%~nxF.spv"
            if !ERRORLEVEL! equ 0 (
                echo Successfully compiled %%F to %%~nxF.spv
                set /a COMPILED_FILES+=1
            ) else (
                echo Error: Failed to compile %%F
            )
        )
    )
)

pause
:: Summary
if !COMPILED_FILES! equ 0 (
    echo No shader files found to compile.
) else (
    echo Compiled !COMPILED_FILES! shader file(s).
)

exit /b 0