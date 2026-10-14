@echo off
rem Deletes the builder's build tools (files\native, or C:\rood-boi-build: MSYS2, Python, the
rem source code and the build, about 10 GB). Your rood-boi folder, saves and settings are kept.
rem Running "Build rood-boi.bat" again sets them up again.
title rood-boi builder - remove build tools
cd /d "%~dp0"
echo This deletes the build tools (MSYS2, Python, the source code and the build, about 10 GB).
echo Your rood-boi folder, saves and settings are kept.
set /p answer=Delete them? [Y/N]
if /i not "%answer:~0,1%"=="y" goto end
if exist "files\native" rd /s /q "files\native"
if exist "%SystemDrive%\rood-boi-build\msys64" rd /s /q "%SystemDrive%\rood-boi-build"
echo Done.
:end
echo.
pause
