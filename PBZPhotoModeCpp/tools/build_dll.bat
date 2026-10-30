@echo off
rem ============================================================
rem  Build the UE4SS DLL (P1). Requires UE4SS source + UEPseudo.
rem  Usage:
rem     tools\build_dll.bat [UE4SS_SOURCE_DIR]
rem  Default UE4SS_SOURCE_DIR = _ref\ue4ss-src
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "UE4SS_DIR=%~1"
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

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found.
    exit /b 1
)

rem rustup ships cargo here; UE4SS build needs it on PATH
if exist "%USERPROFILE%\.cargo\bin\cargo.exe" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

echo [1/3] Loading MSVC environment...
call "%VCVARS%" >nul || (echo [ERROR] vcvars64 failed & exit /b 1)

set "GEN=NMake Makefiles"
where ninja >nul 2>nul && set "GEN=Ninja"

echo [2/3] Configuring (generator: !GEN!)
rem UE4SS 用自定义构建类型，不是 Release
cmake -S . -B build-dll -G "!GEN!" -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DPBZPM_WITH_UE4SS=ON -DPBZPM_UE4SS_DIR="%UE4SS_DIR%"
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
