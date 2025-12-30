@echo off
setlocal

echo ASIO Mini Host - Build Script
echo ============================
echo.

:: Check for Visual Studio
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Visual Studio C++ compiler not found.
    echo Please run this from a Developer Command Prompt.
    echo.
    echo To open one:
    echo   1. Open Start Menu
    echo   2. Search for "Developer Command Prompt"
    echo   3. Run it and navigate to this folder
    echo.
    pause
    exit /b 1
)

:: Check for CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo CMake not found, using direct compilation...
    goto :direct_build
)

:: Build with CMake
echo Using CMake...
if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo Trying Visual Studio 16 2019...
    cmake .. -G "Visual Studio 16 2019" -A x64
)
if %errorlevel% neq 0 (
    echo CMake configuration failed, trying direct build...
    cd ..
    goto :direct_build
)

cmake --build . --config Release
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Build successful!
echo Executable: build\bin\ASIOMiniHost.exe
goto :done

:direct_build
echo.
echo Building directly with MSVC...

if not exist build mkdir build

cl /nologo /EHsc /O2 /MT /DUNICODE /D_UNICODE /W3 ^
   /Fobuild\ ^
   src\main.cpp src\asio_host.cpp ^
   /link /SUBSYSTEM:WINDOWS ^
   ole32.lib oleaut32.lib uuid.lib shell32.lib advapi32.lib ^
   /OUT:build\ASIOMiniHost.exe

if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Build successful!
echo Executable: build\ASIOMiniHost.exe

:done
echo.
pause
