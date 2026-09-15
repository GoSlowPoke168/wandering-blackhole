@echo off
rem Launches the overlay. Quit it from the tray icon, or with Ctrl+Alt+Q.
rem Build first with native\build.bat if native\BlackHolePomodoro.exe is missing.
cd /d "%~dp0"
if not exist native\BlackHolePomodoro.exe call native\build.bat
start "" native\BlackHolePomodoro.exe
