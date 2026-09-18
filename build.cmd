@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if not exist build\build.ninja (
  echo [routerAI] Build files are missing; configuring first...
  call configure.cmd
  if errorlevel 1 exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
  echo [routerAI] Visual Studio Installer / vswhere was not found.
  exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"

if not defined VSINSTALL (
  echo [routerAI] Visual Studio C++ x64 build tools were not found.
  exit /b 1
)

echo [routerAI] Activating MSVC x64 environment for build...
call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if /i not "!VSCMD_ARG_TGT_ARCH!"=="x64" (
  echo [routerAI] MSVC target architecture is "!VSCMD_ARG_TGT_ARCH!", expected x64.
  exit /b 1
)

cmake --build build -j 8
exit /b %errorlevel%
