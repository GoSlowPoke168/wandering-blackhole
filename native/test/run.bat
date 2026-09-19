@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo Visual Studio was not found. & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio is installed without the C++ desktop workload. & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo Could not activate MSVC. & exit /b 1 )
cl /nologo /EHsc /O2 /std:c++17 /W3 test-clocks.cpp /Fe:test-clocks.exe /Fo:test-clocks.obj
if errorlevel 1 exit /b 1
.\test-clocks.exe
