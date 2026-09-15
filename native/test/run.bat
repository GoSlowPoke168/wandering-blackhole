@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo Could not activate MSVC. & exit /b 1 )
cl /nologo /EHsc /O2 /std:c++17 /W3 test-clocks.cpp /Fe:test-clocks.exe /Fo:test-clocks.obj
if errorlevel 1 exit /b 1
.\test-clocks.exe
