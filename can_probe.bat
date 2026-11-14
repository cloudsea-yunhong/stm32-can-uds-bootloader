@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo 发送 UDS 10 03...
".venv\Scripts\python.exe" tools\can_probe.py
pause
