@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo 测试 PCAN 适配器...
".venv\Scripts\python.exe" tools\test_pcan.py
pause
