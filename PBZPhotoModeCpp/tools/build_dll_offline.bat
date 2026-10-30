@echo off
rem ============================================================
rem  离线构建 DLL：与 build_dll.bat 相同，但跳过 FetchContent 的
rem  git update 步骤（GitHub 不通时用；要求 build-dll\_deps 里
rem  已有一次成功构建留下的 *-src 目录）。
rem  Usage: tools\build_dll_offline.bat [UE4SS_SOURCE_DIR]
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "UE4SS_DIR=%~1"
if "%UE4SS_DIR%"=="" set "UE4SS_DIR=%CD%\..\_ref\ue4ss-src"

if not exist "%UE4SS_DIR%\CMakeLists.txt" (
    echo [ERROR] UE4SS source not found: %UE4SS_DIR%
    exit /b 1
)

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found.
    exit /b 1
)

if exist "%USERPROFILE%\.cargo\bin\cargo.exe" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

echo [1/3] Loading MSVC environment...
call "%VCVARS%" >nul || (echo [ERROR] vcvars64 failed & exit /b 1)

set "GEN=NMake Makefiles"
where ninja >nul 2>nul && set "GEN=Ninja"

echo [2/3] Configuring offline (FETCHCONTENT_FULLY_DISCONNECTED=ON)
cmake -S . -B build-dll -G "!GEN!" -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DPBZPM_WITH_UE4SS=ON -DPBZPM_UE4SS_DIR="%UE4SS_DIR%" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
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
