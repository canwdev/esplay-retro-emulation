@echo off
setlocal EnableExtensions

set "ROOT_DIR=%~dp0"
set "LAUNCHER_DIR=%ROOT_DIR%launcher"
set "BUILD_DIR=%LAUNCHER_DIR%\build"
set "DIST_DIR=%ROOT_DIR%dist"

echo [INFO] Root      : %ROOT_DIR%
echo [INFO] Build dir : %BUILD_DIR%
echo [INFO] Dist dir  : %DIST_DIR%

if not exist "%BUILD_DIR%\launcher.bin" (
  echo [ERROR] Build artifact not found: %BUILD_DIR%\launcher.bin
  echo [HINT] Please run these commands first:
  echo        cd /d "%LAUNCHER_DIR%"
  echo        idf.py build
  exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if not exist "%DIST_DIR%\bootloader" mkdir "%DIST_DIR%\bootloader"
if not exist "%DIST_DIR%\partition_table" mkdir "%DIST_DIR%\partition_table"

call :copy_required "%BUILD_DIR%\launcher.bin" "%DIST_DIR%\launcher.bin"
call :copy_required "%BUILD_DIR%\launcher.elf" "%DIST_DIR%\launcher.elf"
call :copy_required "%BUILD_DIR%\launcher.map" "%DIST_DIR%\launcher.map"
call :copy_required "%BUILD_DIR%\bootloader\bootloader.bin" "%DIST_DIR%\bootloader\bootloader.bin"
call :copy_required "%BUILD_DIR%\partition_table\partition-table.bin" "%DIST_DIR%\partition_table\partition-table.bin"
call :copy_required "%BUILD_DIR%\ota_data_initial.bin" "%DIST_DIR%\ota_data_initial.bin"

call :copy_optional "%BUILD_DIR%\flash_args" "%DIST_DIR%\flash_args"
call :copy_optional "%BUILD_DIR%\flash_app_args" "%DIST_DIR%\flash_app_args"
call :copy_optional "%BUILD_DIR%\flasher_args.json" "%DIST_DIR%\flasher_args.json"
call :copy_optional "%LAUNCHER_DIR%\partitions.csv" "%DIST_DIR%\partitions.csv"
call :copy_optional "%LAUNCHER_DIR%\sdkconfig" "%DIST_DIR%\sdkconfig"
call :copy_optional "%LAUNCHER_DIR%\sdkconfig.defaults" "%DIST_DIR%\sdkconfig.defaults"

echo.
echo [OK] Build artifacts exported to:
echo      %DIST_DIR%
exit /b 0

:copy_required
if not exist "%~1" (
  echo [ERROR] Missing required file: %~1
  exit /b 1
)
copy /Y "%~1" "%~2" >nul
if errorlevel 1 (
  echo [ERROR] Failed to copy: %~1
  exit /b 1
)
echo [COPY] %~nx1
exit /b 0

:copy_optional
if exist "%~1" (
  copy /Y "%~1" "%~2" >nul
  if errorlevel 1 (
    echo [ERROR] Failed to copy: %~1
    exit /b 1
  )
  echo [COPY] %~nx1
)
exit /b 0
