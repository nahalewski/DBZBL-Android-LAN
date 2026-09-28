@echo off
rem Start the game from the build folder using game_data_root next to this file.
cd /d "%~dp0"
set "EXE=%CD%\out\build\win-amd64-relwithdebinfo\burstlimit.exe"
if not exist "%EXE%" set "EXE=%CD%\burstlimit.exe"
start "" "%EXE%" --game_data_root "%CD%\game_data_root" %*
