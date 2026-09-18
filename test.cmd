@echo off
setlocal

cd /d "%~dp0"

call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1

call build.cmd
if errorlevel 1 exit /b 1

ctest --test-dir build --output-on-failure
exit /b %errorlevel%
