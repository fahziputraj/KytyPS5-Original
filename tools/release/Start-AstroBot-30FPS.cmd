@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-AstroBot-Preview.ps1" -Mode Fixed30 %*
if errorlevel 1 pause
