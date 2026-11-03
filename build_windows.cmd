@echo off
setlocal enabledelayedexpansion

:: Check MSYS2 installation
if not exist "C:\msys64\usr\bin\bash.exe" (
    echo [ERROR] MSYS2 not found at C:\msys64
    echo Please install MSYS2 from https://www.msys2.org/
    pause
    exit /b 1
)

:: If current path is UNC (starts with \\), map to virtual drive B:
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~0,2%"=="\\" (
    if not exist B:\ (
        subst B: "%SCRIPT_DIR:~0,-1%"
        echo [INFO] Mapped UNC path to drive B:
    )
    cd /d B:\
) else (
    cd /d "%SCRIPT_DIR%"
)

echo [INFO] Starting Windows build via MSYS2 CLANG64...
set "MSYSTEM=CLANG64"
set "CHERE_INVOKING=1"
C:\msys64\usr\bin\bash.exe -l extra/tools/build_windows.sh

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Windows package ready in dist\
) else (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%
)

if "%1"=="" pause
