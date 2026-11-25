@echo off
chcp 65001 >nul
echo 刷写 B 区（CAN OTA，自动交权）...
cd /d "%~dp0host"
"..\.venv\Scripts\python.exe" cli.py --backend pcan --channel PCAN_USBBUS1 flash --bin "..\firmware\release\app_b.bin" --addr 0x08040000 --area 1
pause