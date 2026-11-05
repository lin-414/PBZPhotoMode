@echo off
rem ============================================================
rem  Build and run the core unit tests (no game, no UE4SS needed).
rem  Usage: tools\build_tests.bat
rem ------------------------------------------------------------
rem  MAINTENANCE: the source list below is a HAND-WRITTEN COPY of the
rem  CMake targets in CMakeLists.txt:
rem      CMakeLists.txt:14-25  ->  target pbzpm_core  (10 sources)
rem      CMakeLists.txt:31-34  ->  target pbzpm_tests (4 test TUs)
rem  Both places must be kept in sync. If you add a src/core/*.cpp and
rem  forget this file, you silently get a test binary with missing
rem  symbols. Sources are listed in alphabetical order to make a
rem  line-by-line diff against CMakeLists.txt easy.
rem  ASCII only on purpose: cmd.exe with codepage 936 misparses
rem  multi-byte comments and swallows line breaks.
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
rem test TUs (same as CMakeLists.txt:31-34)
cl /nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /MD ^
   /Fe:build\pbzpm_tests.exe /Fo:build\ /Fd:build\ ^
   tests\main.cpp tests\test_mod_runtime.cpp tests\test_input_pure.cpp tests\test_pbzfm_validation.cpp ^
   src\core\config.cpp ^
   src\core\gamepad.cpp ^
   src\core\hook_registry.cpp ^
   src\core\i18n_logic.cpp ^
   src\core\input_logic.cpp ^
   src\core\mod_runtime.cpp ^
   src\core\photo_mode.cpp ^
   src\core\vk.cpp ^
   src\pbzfm_client_lang.cpp ^
   src\pbzfm_schema_map.cpp ^
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
