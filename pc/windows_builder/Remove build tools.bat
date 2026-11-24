@echo off
rem Deletes the builder's private Ubuntu (files\linux, about 7 GB). Your rood-boi folder,
rem saves and settings are kept. Running "Build rood-boi.bat" again sets it up again.
title rood-boi builder - remove build tools
echo This deletes the build tools (the private Ubuntu in files\linux, about 7 GB).
echo Your rood-boi folder, saves and settings are kept.
set /p answer=Delete them? [Y/N] 
if /i not "%answer:~0,1%"=="y" goto end
wsl.exe --unregister rood-boi-build
echo Done.
:end
echo.
pause
