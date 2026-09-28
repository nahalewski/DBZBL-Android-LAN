@echo off
rem Git on Windows checks out symlinks as small text files unless symlink
rem support is enabled. libmspack inside the SDK relies on symlinks, so replace
rem each placeholder with a copy of the file it points to.
setlocal
set "MSPACK=%~dp0..\thirdparty\rexglue-sdk\thirdparty\libmspack"
if not exist "%MSPACK%" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root = (Resolve-Path '%MSPACK%').Path;" ^
  "git -C $root ls-files -s | ForEach-Object { $p = $_ -split '\s+', 4; if ($p[0] -eq '120000') {" ^
  "  $link = Join-Path $root $p[3]; $item = Get-Item $link -Force;" ^
  "  if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and $item.Length -lt 512) {" ^
  "    $target = Join-Path (Split-Path $link) (Get-Content $link -Raw).Trim();" ^
  "    if (Test-Path $target) { Copy-Item $target $link -Force } } } }"
exit /b 0
