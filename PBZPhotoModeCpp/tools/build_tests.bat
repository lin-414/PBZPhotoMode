@echo off
rem ============================================================
rem  Build and run the core unit tests (no game, no UE4SS needed).
rem  Usage: tools\build_tests.bat
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found. Install Visual Studio 2022 Build Tools with "Desktop development with C++".
    exit /b 1
)

echo [1/3] Loading MSVC environment...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed
    exit /b 1
)

if not exist build mkdir build

echo [2/3] Compiling tests
cl /nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /MD ^
   /Fe:build\pbzpm_tests.exe /Fo:build\ /Fd:build\ ^
   tests\main.cpp ^
   src\core\config.cpp src\core\gamepad.cpp src\core\input_logic.cpp src\core\photo_mode.cpp src\core\vk.cpp ^
   /I. /Isrc
if errorlevel 1 (
    echo [ERROR] compile failed
    exit /b 1
)

echo [3/3] Running tests
build\pbzpm_tests.exe
set "RC=!errorlevel!"
echo.
if "!RC!"=="0" (echo RESULT: all checks passed) else (echo RESULT: failures, exit code !RC!)
exit /b !RC!
