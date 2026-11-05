@echo off
rem ============================================================
rem  Build the UE4SS DLL (P1). Requires UE4SS source + UEPseudo.
rem  Usage:
rem     tools\build_dll.bat [UE4SS_SOURCE_DIR] [--offline]
rem  Default UE4SS_SOURCE_DIR = ..\_ref\ue4ss-src
rem  --offline : adds -DFETCHCONTENT_FULLY_DISCONNECTED=ON, which skips
rem              FetchContent's git update step (use when GitHub is
rem              unreachable). Requires a previous successful build so
rem              that build-dll\_deps already holds the *-src trees.
rem  This script merges the former build_dll.bat + build_dll_offline.bat;
rem  the vcvars64 probe now lives in tools\_vcvars.bat (single place).
rem  ASCII only on purpose: cmd.exe with codepage 936 misparses
rem  multi-byte comments and swallows line breaks.
rem ============================================================
setlocal enabledelayedexpansion
rem Capture the tools dir BEFORE any shift: `shift` reassigns %0, and after that
rem %~dp0 resolves against the current directory instead of this script.
set "TOOLS_DIR=%~dp0"
cd /d "%TOOLS_DIR%.."

rem ---- args: positional = UE4SS dir; --offline may appear anywhere ----
set "UE4SS_DIR="
set "OFFLINE=0"
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--offline" (
    set "OFFLINE=1"
    shift
    goto parse_args
)
set "UE4SS_DIR=%~1"
shift
goto parse_args
:args_done

if "%UE4SS_DIR%"=="" set "UE4SS_DIR=%CD%\..\_ref\ue4ss-src"

if not exist "%UE4SS_DIR%\CMakeLists.txt" (
    echo [ERROR] UE4SS source not found: %UE4SS_DIR%
    exit /b 1
)
if not exist "%UE4SS_DIR%\deps\first\Unreal\CMakeLists.txt" (
    echo [ERROR] UEPseudo submodule missing.
    echo         Run: git -C "%UE4SS_DIR%" submodule update --init --recursive
    echo         Requires GitHub account linked to Epic Games.
    exit /b 1
)

echo [1/3] Loading MSVC environment...
call "%TOOLS_DIR%_vcvars.bat"
if errorlevel 1 exit /b 1

set "GEN=NMake Makefiles"
where ninja >nul 2>nul && set "GEN=Ninja"

set "OFFLINE_FLAG="
if "!OFFLINE!"=="1" set "OFFLINE_FLAG=-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
if "!OFFLINE!"=="1" echo [2/3] Configuring OFFLINE - skipping FetchContent git update - generator=!GEN!
if not "!OFFLINE!"=="1" echo [2/3] Configuring - generator=!GEN!
rem UE4SS uses a custom build type, not Release
cmake -S . -B build-dll -G "!GEN!" -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DPBZPM_WITH_UE4SS=ON -DPBZPM_UE4SS_DIR="%UE4SS_DIR%" !OFFLINE_FLAG!
if errorlevel 1 (
    echo [ERROR] cmake configure failed
    exit /b 1
)

echo [3/3] Building
cmake --build build-dll --config Game__Shipping__Win64
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo.
echo Done. Look for build-dll\PBZPhotoModeCpp.dll
exit /b 0
