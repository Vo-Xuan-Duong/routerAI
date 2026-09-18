@echo off
setlocal

cd /d "%~dp0"

call build.cmd
if errorlevel 1 exit /b 1

if not exist build\router.exe (
  echo [routerAI] build\router.exe was not produced.
  exit /b 1
)

set "PATH=%CD%\build\vcpkg_installed\x64-windows\bin;%PATH%"
build\router.exe
exit /b %errorlevel%
