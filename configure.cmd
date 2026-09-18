@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\dev\vcpkg"

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo [routerAI] vcpkg was not found at "%VCPKG_ROOT%".
  echo Set VCPKG_ROOT to your vcpkg directory and run configure.cmd again.
  exit /b 1
)

call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1

where cmake >nul 2>&1 || (
  echo [routerAI] cmake was not found on PATH.
  exit /b 1
)

where ninja >nul 2>&1 || (
  echo [routerAI] ninja was not found on PATH.
  exit /b 1
)

if exist build\CMakeCache.txt (
  findstr /i /c:"mingw" build\CMakeCache.txt >nul 2>&1
  if not errorlevel 1 (
    echo [routerAI] Removing stale MinGW build directory...
    rmdir /s /q build
  )
)

echo [routerAI] Configuring Release with MSVC + Ninja + x64-windows...
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

exit /b %errorlevel%
