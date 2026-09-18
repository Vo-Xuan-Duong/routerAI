@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\dev\vcpkg"

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo [routerAI] vcpkg was not found at "%VCPKG_ROOT%".
  echo Set VCPKG_ROOT to your vcpkg directory and run configure.cmd again.
  exit /b 1
)

where cmake >nul 2>&1 || (
  echo [routerAI] cmake was not found on PATH.
  exit /b 1
)

where ninja >nul 2>&1 || (
  echo [routerAI] ninja was not found on PATH.
  exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "!VSWHERE!" (
    echo [routerAI] MSVC was not found.
    echo Install Visual Studio 2022 or Build Tools 2022 with "Desktop development with C++".
    exit /b 1
  )

  set "VSINSTALL="
  for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"

  if not defined VSINSTALL (
    echo [routerAI] Visual Studio C++ build tools were not found.
    echo Install the "Desktop development with C++" workload and try again.
    exit /b 1
  )

  if not exist "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" (
    echo [routerAI] vcvars64.bat was not found under "!VSINSTALL!".
    exit /b 1
  )

  echo [routerAI] Activating MSVC x64 environment...
  call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul
  if errorlevel 1 exit /b 1
)

where cl >nul 2>&1 || (
  echo [routerAI] cl.exe is still unavailable after MSVC environment setup.
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
