@echo off
echo 刷写 A 区（自动交权）...
cd /d "%~dp0host"
"..\.venv\Scripts\python.exe" cli.py --backend pcan --channel PCAN_USBBUS1 flash --bin "..\firmware\app\MDK-ARM\Objects\app.bin" --addr 0x08010000 --area 0
pause
