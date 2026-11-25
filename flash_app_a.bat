@echo off
chcp 65001 >nul
echo 刷写 A 区（CAN OTA，自动交权）...
cd /d "%~dp0host"
"..\.venv\Scripts\python.exe" cli.py --backend pcan --channel PCAN_USBBUS1 flash --bin "..\firmware\release\app_a.bin" --addr 0x08010000 --area 0
pause