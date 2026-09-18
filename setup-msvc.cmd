@echo off
rem Activates MSVC x64 build environment if cl.exe is not already in PATH.

where cl >nul 2>&1
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [routerAI] MSVC was not found.
  echo Install Visual Studio 2022 or Build Tools 2022 with "Desktop development with C++".
  exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"

if not defined VSINSTALL (
  echo [routerAI] Visual Studio C++ build tools were not found.
  echo Install the "Desktop development with C++" workload and try again.
  exit /b 1
)

if not exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
  echo [routerAI] vcvars64.bat was not found under "%VSINSTALL%".
  exit /b 1
)

echo [routerAI] Activating MSVC x64 environment...
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin" set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja" set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

where cl >nul 2>&1 || (
  echo [routerAI] cl.exe is still unavailable after MSVC environment setup.
  exit /b 1
)

exit /b 0
