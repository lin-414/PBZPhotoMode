@echo off
rem ============================================================
rem  Regenerate the commented default config from the code.
rem  This is the ONLY correct way to produce config.ini.example
rem  (that file is a derivative - do not hand-edit it).
rem  Usage: tools\gen_config.bat [output-file]
rem     Default output = config.ini.example. You may pass another
rem     path, e.g.  tools\gen_config.bat dist\PBZPhotoModeCpp\config.ini
rem  tools\gen_config.cpp is tracked in git (moved out of build\ in
rem  2.0.0), so a fresh clone can run this directly - the old version
rem  compiled a .cpp that .gitignore excluded from the repo.
rem  ASCII only on purpose: cmd.exe with codepage 936 misparses
rem  multi-byte comments and swallows line breaks.
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0.."

echo [1/2] Loading MSVC environment...
call "%~dp0_vcvars.bat"
if errorlevel 1 exit /b 1

set "OUT=%~1"
if "%OUT%"=="" set "OUT=config.ini.example"

if not exist build mkdir build

echo [2/2] Generating %OUT% from default_config_text()
cl /nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /MD ^
   /Fe:build\gen_config.exe /Fo:build\ /Fd:build\ ^
   tools\gen_config.cpp ^
   src\core\config.cpp ^
   /I. /Isrc
if errorlevel 1 exit /b 1

build\gen_config.exe > "%OUT%"
if errorlevel 1 exit /b 1
echo [OK] %OUT% refreshed from default_config_text()
exit /b 0
