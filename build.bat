@echo off
setlocal
rem Dragon Ball Z: Burst Limit Recompiled - Windows build (RelWithDebInfo).
rem Requires Visual Studio 2022 (C++ workload), LLVM clang, CMake and Ninja.
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio 2022 with the C++ workload was not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "game_data_root\default.xex" (
  echo game_data_root\default.xex not found. Copy your game files first - see README.md.
  exit /b 1
)

call scripts\fix_sdk_symlinks.bat || exit /b 1

set "PRESET=win-amd64-relwithdebinfo"
if not exist "out\build\%PRESET%\build.ninja" (
  cmake --preset %PRESET% %* || exit /b 1
)
rem First pass generates the recompiled sources, second pass picks them up.
cmake --build "out\build\%PRESET%" --target burstlimit_codegen || exit /b 1
cmake "out\build\%PRESET%" || exit /b 1
cmake --build "out\build\%PRESET%" --target burstlimit || exit /b 1

echo.
echo Build finished: out\build\%PRESET%\burstlimit.exe
