@echo off
chcp 65001 >nul
setlocal
set "CLI=D:\STLINK\ST-LINK Utility\ST-LINK_CLI.exe"
set "ROOT=%~dp0firmware"
echo [0/4] 全片擦除（同时清掉旧参数区）
"%CLI%" -c SWD -ME
if errorlevel 1 goto fail
echo [1/4] Bootloader
"%CLI%" -c SWD -P "%ROOT%\bootloader\MDK-ARM\Objects\bootloader.hex" -V after_programming
if errorlevel 1 goto fail
echo [2/4] App-A
"%CLI%" -c SWD -P "%ROOT%\app\MDK-ARM\Objects\app.hex" -V after_programming
if errorlevel 1 goto fail
echo [3/4] App-B
"%CLI%" -c SWD -P "%ROOT%\app\MDK-ARM\Objects_B\app_b.hex" -V after_programming -Rst
if errorlevel 1 goto fail
echo ALL OK
pause
exit /b 0
:fail
echo FLASH FAILED
pause
exit /b 1