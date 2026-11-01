@echo off
rem ============================================================
rem  一次性工具：用核心层 default_config_text() 刷新 dist config.ini
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

cl /nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /MD ^
   /Fe:build\gen_config.exe /Fo:build\ /Fd:build\ ^
   build\gen_config.cpp ^
   src\core\config.cpp ^
   /I. /Isrc
if errorlevel 1 exit /b 1

build\gen_config.exe > dist\PBZPhotoModeCpp\config.ini
if errorlevel 1 exit /b 1
echo [OK] dist config.ini refreshed
exit /b 0
