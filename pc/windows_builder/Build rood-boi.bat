@echo off
rem Double-click to build rood-boi (Vagrant Story for Windows) from your own disc image.
title rood-boi builder
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\build.ps1" -Dropped "%~1"
echo.
pause
