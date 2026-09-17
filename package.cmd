@echo off
setlocal
call "%~dp0build.cmd"
if errorlevel 1 exit /b %errorlevel%

set DIST=%~dp0dist\routerAI
if exist "%~dp0dist" rmdir /s /q "%~dp0dist"
mkdir "%DIST%"
copy /y "%~dp0build\router.exe" "%DIST%\router.exe" >nul
copy /y "%~dp0README.md" "%DIST%\README.md" >nul
copy /y "%~dp0CHANGELOG.md" "%DIST%\CHANGELOG.md" >nul

set DLLDIR=%~dp0build\vcpkg_installed\x64-mingw-dynamic\bin
if exist "%DLLDIR%" (
  for %%F in ("%DLLDIR%\*.dll") do copy /y "%%F" "%DIST%\" >nul
)

powershell -NoProfile -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath '%~dp0dist\routerAI-0.7.0-windows-x64.zip' -Force"
if errorlevel 1 exit /b %errorlevel%

echo Portable package: %~dp0dist\routerAI-0.7.0-windows-x64.zip
endlocal
