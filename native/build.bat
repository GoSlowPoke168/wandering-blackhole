@echo off
rem Builds native\bhp-spike.exe. Same toolchain activation as probes\native\build.bat.
rem The name matters: NVIDIA's driver profiles match on exe name, and a generic one such as
rem overlay.exe gets forced onto the dGPU, under which Desktop Duplication is unsupported.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo Could not activate MSVC. & exit /b 1 )
if not exist obj mkdir obj
cl /nologo /EHsc /O2 /std:c++17 /W3 /DUNICODE /D_UNICODE /Fo.\obj\ ^
   src\main.cpp src\overlay.cpp /Fe:bhp-spike.exe /link /SUBSYSTEM:CONSOLE
