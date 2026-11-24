@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
    ) else (
        echo CMake was not found. Install CMake and add it to PATH.
        exit /b 1
    )
) else (
    set "CMAKE=cmake"
)

if not exist "build\CMakeCache.txt" (
    echo Configuring...
    "%CMAKE%" -S . -B build -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)

echo Building...
"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 exit /b 1

if exist "build\Release\blackhole.exe" (
    echo.
    echo Built build\Release\blackhole.exe
    echo Run it with: build\Release\blackhole.exe
) else if exist "build\blackhole.exe" (
    echo.
    echo Built build\blackhole.exe
    echo Run it with: build\blackhole.exe
)

exit /b 0
