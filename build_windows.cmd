@echo off
setlocal enabledelayedexpansion

:: Clean option
if /I "%1"=="clean" (
    echo [INFO] Cleaning build directories...
    rmdir /s /q out\win-native out\win-dev out\launcher-win 2>nul
    echo [SUCCESS] Clean completed.
    exit /b 0
)
if /I "%1"=="--clean" (
    echo [INFO] Cleaning build directories...
    rmdir /s /q out\win-native out\win-dev out\launcher-win 2>nul
    echo [SUCCESS] Clean completed.
    exit /b 0
)

:: Check MSYS2 installation
if not exist "C:\msys64\usr\bin\bash.exe" (
    echo [ERROR] MSYS2 not found at C:\msys64
    echo Please install MSYS2 from https://www.msys2.org/
    pause
    exit /b 1
)

cd /d "%~dp0"

echo [INFO] Starting Windows build via MSYS2 CLANG64...
set "MSYSTEM=CLANG64"
set "CHERE_INVOKING=1"
set "DEV_ARG="
if /I "%1"=="--DEV" set "DEV_ARG=--DEV"
if /I "%1"=="--dev" set "DEV_ARG=--DEV"
if "%DEV_ARG%"=="--DEV" (
    echo [INFO] MODE: DEV - fast build, no launcher packaging/zip
) else (
    echo [INFO] MODE: RELEASE - full build with launcher packaging/zip
)
C:\msys64\usr\bin\bash.exe -l extra/tools/build_windows.sh %DEV_ARG%

if %ERRORLEVEL% equ 0 (
    if "%DEV_ARG%"=="--DEV" (
        echo [SUCCESS] DEV build done - artifacts in out\win-dev\ ^(skipped launcher/zip^)
    ) else (
        echo [SUCCESS] Windows package ready in dist\
    )
) else (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%
)

if "%1"=="" pause
