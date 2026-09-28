@echo off
setlocal
rem Host an online match over Radmin VPN (or any LAN/VPN).
cd /d "%~dp0.."
set "EXE=%CD%\out\build\win-amd64-relwithdebinfo\burstlimit.exe"
if not exist "%EXE%" set "EXE=%CD%\burstlimit.exe"

set /p REX_XNET_IP=Enter YOUR Radmin VPN IPv4: 
if "%REX_XNET_IP%"=="" exit /b 1
set "REX_XNET_SEARCH_IP="

"%EXE%" --game_data_root "%CD%\game_data_root" --log_file "%CD%\online-host.log"
