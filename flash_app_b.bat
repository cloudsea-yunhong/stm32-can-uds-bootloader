@echo off
echo 刷写 B 区（自动交权）...
cd /d "%~dp0host"
"..\.venv\Scripts\python.exe" cli.py --backend pcan --channel PCAN_USBBUS1 flash --bin "..\firmware\app\MDK-ARM\Objects_B\app_b.bin" --addr 0x08040000 --area 1
pause
