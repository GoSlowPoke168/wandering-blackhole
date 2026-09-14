@echo off
rem Launches the overlay. The console window stays open while it runs; closing it
rem quits the app. For a no-console launch, double-click "Black Hole Pomodoro.vbs".
cd /d "%~dp0"
node_modules\.bin\electron.cmd .
