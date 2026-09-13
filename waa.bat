@echo off
REM Build wandaac (if needed) and compile+run a Wandaa program.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
