@echo off
rem Builds native\WanderingBlackHole.exe.
rem The name matters: NVIDIA's driver profiles match on exe name, and a generic one such as
rem overlay.exe gets forced onto the dGPU, under which Desktop Duplication is unsupported.
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo Visual Studio was not found. & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio is installed without the C++ desktop workload. & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo Could not activate MSVC. & exit /b 1 )
if not exist obj mkdir obj
cl /nologo /EHsc /O2 /std:c++17 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /Fo.\obj\ ^
   src\main.cpp src\app.cpp src\overlay.cpp src\renderer.cpp src\hud.cpp src\config.cpp src\still.cpp ^
   /Fe:WanderingBlackHole.exe /link /SUBSYSTEM:WINDOWS
