@echo off
setlocal

set VERSION=
for /f "tokens=3" %%V in ('findstr /b /c:"project(routerAI VERSION " "%~dp0CMakeLists.txt"') do set VERSION=%%V
if not defined VERSION (
  echo Could not resolve routerAI version from CMakeLists.txt.
  exit /b 1
)

call "%~dp0build.cmd"
if errorlevel 1 exit /b %errorlevel%

set DIST=%~dp0dist\routerAI
if exist "%~dp0dist" rmdir /s /q "%~dp0dist"
mkdir "%DIST%"
copy /y "%~dp0build\router.exe" "%DIST%\router.exe" >nul
copy /y "%~dp0README.md" "%DIST%\README.md" >nul
copy /y "%~dp0CHANGELOG.md" "%DIST%\CHANGELOG.md" >nul

rem vcpkg dynamic libraries used by the development MinGW triplet.
set DLLDIR=%~dp0build\vcpkg_installed\x64-mingw-dynamic\bin
if exist "%DLLDIR%" (
  for %%F in ("%DLLDIR%\*.dll") do copy /y "%%F" "%DIST%\" >nul
)

rem MinGW compiler runtime libraries are not stored under vcpkg_installed.
rem Copy them when present so the local ZIP can run on a machine without the
rem same MinGW installation. Different distributions use either seh or dw2.
for %%D in (libstdc++-6.dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libwinpthread-1.dll) do (
  for /f "delims=" %%P in ('where %%D 2^>nul') do (
    if not exist "%DIST%\%%D" copy /y "%%P" "%DIST%\%%D" >nul
  )
)

powershell -NoProfile -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath '%~dp0dist\routerAI-%VERSION%-windows-x64.zip' -Force"
if errorlevel 1 exit /b %errorlevel%

echo Portable package: %~dp0dist\routerAI-%VERSION%-windows-x64.zip
endlocal
