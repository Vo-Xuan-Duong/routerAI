@echo off
setlocal

cd /d "%~dp0"

call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1

if not exist build\CMakeCache.txt (
  call configure.cmd
  if errorlevel 1 exit /b 1
)

cmake --build build -j 8
exit /b %errorlevel%
