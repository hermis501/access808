@echo off
rem Builds Access808 and its installer from scratch.
rem   1. Configures CMake (first run only)
rem   2. Builds the VST3 plugin (Release)
rem   3. Builds and runs the tests
rem   4. Builds the installer with Inno Setup 6
rem Result: build\installer\Access808-<version>-Windows-Setup.exe
rem
rem Usage: build-installer.bat            (full build with tests)
rem        build-installer.bat notests    (skip the tests)

setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found. Install CMake and try again.
    goto :fail
)

if not exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" if not exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    echo ERROR: Inno Setup 6 was not found. Install it from jrsoftware.org and try again.
    goto :fail
)

if not exist "build\CMakeCache.txt" (
    echo.
    echo Step 1 of 4: configuring the project. The first time takes about two minutes.
    cmake -S . -B build -G "Visual Studio 18 2026" -A x64
    if errorlevel 1 (
        echo ERROR: configuring failed.
        goto :fail
    )
) else (
    echo.
    echo Step 1 of 4: project already configured.
)

echo.
echo Step 2 of 4: building the plugin.
cmake --build build --config Release --target Access808_VST3 -- -m
if errorlevel 1 (
    echo ERROR: building the plugin failed.
    goto :fail
)

if /i "%~1"=="notests" (
    echo.
    echo Step 3 of 4: tests skipped.
    goto :installer
)

echo.
echo Step 3 of 4: building and running the tests.
cmake --build build --config Release --target EngineTests ProcessorTests -- -m
if errorlevel 1 (
    echo ERROR: building the tests failed.
    goto :fail
)
build\Release\EngineTests.exe
if errorlevel 1 (
    echo ERROR: engine tests failed.
    goto :fail
)
build\Release\ProcessorTests.exe
if errorlevel 1 (
    echo ERROR: processor tests failed.
    goto :fail
)

:installer
echo.
echo Step 4 of 4: building the installer.
cmake --build build --config Release --target installer
if errorlevel 1 (
    echo ERROR: building the installer failed.
    goto :fail
)

echo.
echo Done. The installer is in the build\installer folder:
dir /b build\installer\*.exe
echo.
pause
exit /b 0

:fail
echo.
echo The build stopped because of the error above.
pause
exit /b 1
