@echo off
rem ============================================================
rem  Run the core unit tests, then write the actual assertion count
rem  to docs\test_count.txt (single source referenced by the docs).
rem  Usage: tools\update_test_count.bat
rem ============================================================
setlocal
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update_test_count.ps1"
exit /b %errorlevel%
