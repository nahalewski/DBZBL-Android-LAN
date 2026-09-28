@echo off
setlocal
rem Join an online match over Radmin VPN (or any LAN/VPN).
cd /d "%~dp0.."
set "EXE=%CD%\out\build\win-amd64-relwithdebinfo\burstlimit.exe"
if not exist "%EXE%" set "EXE=%CD%\burstlimit.exe"

set /p REX_XNET_IP=Enter YOUR Radmin VPN IPv4: 
if "%REX_XNET_IP%"=="" exit /b 1
set /p REX_XNET_SEARCH_IP=Enter the HOST's Radmin VPN IPv4: 
if "%REX_XNET_SEARCH_IP%"=="" exit /b 1

"%EXE%" --game_data_root "%CD%\game_data_root" --log_file "%CD%\online-join.log"
