@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" -Action Open
if errorlevel 1 pause
