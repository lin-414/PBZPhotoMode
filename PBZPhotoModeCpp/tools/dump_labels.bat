@echo off
cd /d "%~dp0.."
set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
cl /nologo /std:c++20 /EHsc /utf-8 /MD /Fe:build\dump_labels.exe /Fo:build\ /Fd:build\ ^
   build\dump_labels.cpp ^
   src\pbzfm_schema_map.cpp src\core\config.cpp src\core\input_logic.cpp src\core\vk.cpp src\core\gamepad.cpp src\core\photo_mode.cpp ^
   /I. /Isrc
if errorlevel 1 exit /b 1
build\dump_labels.exe > build\labels.txt
if errorlevel 1 exit /b 1
echo [OK] build\labels.txt
