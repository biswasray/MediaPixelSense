@echo off
rem Builds PixelSenseEngine (Release) with the Visual Studio toolchain.
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul || exit /b 1
cmake --preset x64-release || exit /b 1
cmake --build out/build/x64-release || exit /b 1
echo Built out\build\x64-release\PixelSenseEngine\PixelSenseEngine.exe
