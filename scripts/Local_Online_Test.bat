@echo off
setlocal
rem Local online test: two instances on one PC, no Radmin needed.
rem Instance 1 hosts on 127.0.0.1, instance 2 joins from 127.0.0.2.
rem The controller only drives the focused window: click / Alt+Tab to switch.
cd /d "%~dp0.."
set "EXE=%CD%\out\build\win-amd64-relwithdebinfo\burstlimit.exe"
if not exist "%EXE%" set "EXE=%CD%\burstlimit.exe"

set "COMMON=--game_data_root "%CD%\game_data_root" --draw_resolution_scale_x=1 --draw_resolution_scale_y=1 --log_level info"

set "REX_XNET_IP=127.0.0.1"
set "REX_XNET_BIND_IP=127.0.0.1"
set "REX_XNET_SEARCH_IP="
start "Burst Limit HOST" "%EXE%" %COMMON% --log_file "%CD%\local-host.log"

timeout /t 8 /nobreak >nul

set "REX_XNET_IP=127.0.0.2"
set "REX_XNET_BIND_IP=127.0.0.2"
set "REX_XNET_SEARCH_IP=127.0.0.1"
start "Burst Limit JOIN" "%EXE%" %COMMON% --log_file "%CD%\local-join.log"
