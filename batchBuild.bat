@echo off
setlocal

REM %1 is the first argument (e.g., batchBuild.bat myProjectName)
if "%1" == "" (
    echo [ERROR] Please enter a project name.
    echo Usage: %~n0 [project name]
    pause
    exit /b
)

set PROJECT_PATH=".\build\MyDevs\%1.vcxproj"

echo --- Build Started ---
echo.

echo [Release] Building...
MSBuild %PROJECT_PATH% /p:Configuration=Release /p:Platform=x64 /clp:Summary /verbosity:minimal || exit /b

echo.
echo [Debug] Building...
MSBuild %PROJECT_PATH% /p:Configuration=Debug /p:Platform=x64 /clp:Summary /verbosity:minimal || exit /b

echo.
echo [RelWithDebInfo] Building...
MSBuild %PROJECT_PATH% /p:Configuration=RelWithDebInfo /p:Platform=x64 /clp:Summary /verbosity:minimal || exit /b

echo.
echo --- All Builds Completed ---
exit /b

@REM pause