@echo off
rem ============================================================
rem  Shared helper: locate and load the MSVC x64 environment.
rem  Called from build_dll.bat / gen_config.bat:
rem      call "%~dp0_vcvars.bat"
rem      if errorlevel 1 exit /b 1
rem  Do NOT add setlocal here - the env vars must stay in the
rem  caller's context (call /b semantics).
rem  Returns: errorlevel 0 = ready, 1 = vcvars64.bat missing/failed.
rem  (ASCII only on purpose: cmd.exe with codepage 936 misparses
rem   multi-byte comments and swallows line breaks.)
rem ============================================================
set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found.
    echo         Install Visual Studio 2022 Build Tools with "Desktop development with C++".
    exit /b 1
)

rem rustup ships cargo here; the UE4SS build needs it on PATH
if exist "%USERPROFILE%\.cargo\bin\cargo.exe" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed
    exit /b 1
)
exit /b 0
