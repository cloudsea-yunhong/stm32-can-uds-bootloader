# 验证记录

日期：2026-11-14  
环境：Windows + PCAN-USB + STM32F407VET6 + CH340  
代码：当前工作区版本（含 App `params_init()` 修复）

## 1. 上位机自动化测试（无硬件）

```bash
cd can-flasher
.venv\Scripts\python.exe -m unittest discover -s tests -v
```

结果：

```
Ran 15 tests in 1.349s
OK
```

覆盖：CRC 算法、ISO-TP 单帧/多帧/序号/超时、FakeEcu 完整刷写、错误 CRC 拒绝。

## 2. PCAN 与板端链路

```bash
.venv\Scripts\python.exe tools\test_pcan.py
.venv\Scripts\python.exe tools\can_probe.py
```

结果：

```
[ OK ] PCAN_USBBUS1 已打开，500 kbps
已发送 0x7E0: 02 10 03
适配器状态： BusState.ACTIVE
收到 0x7E8: 06 50 03 00 32 01 f4 00
```

## 3. CAN OTA 实测

| 目标 | 固件大小 | 块数 | 耗时 | 结果 |
|---|---|---|---|---|
| App-A | 21216 字节 | 34 | 2.29 s | CRC 通过，切 A 区 |
| App-B | 21216 字节 | 34 | 2.71 s | CRC 通过，切 B 区 |

连续执行 A → B 两次均成功，复位后 `22 FF00` 读回：

```
A 区：DID 0xFF00 -> 00
B 区：DID 0xFF00 -> 01
```

串口启动日志确认：

```
=== CAN Bootloader 启动 ===
[参数] 记录区=B 序号=... 启动区=B
[启动] 跳转到 App：0x08040000
[参数] 记录区=B 序号=... 启动区=B
=== 应用程序 V1.2.3 @ 0x08040000 ===
[应用] 向量表 = 0x08040000
[CAN] 500 kbps，接收 ID 0x7E0
[应用] UDS 就绪
[应用] 启动确认成功
```

## 4. ST-Link 全片恢复

```bash
flash_all.bat
```

流程：`-ME` 全片擦除 → Bootloader → App-A → App-B → 复位。  
结果：`ALL OK`；参数区被清空后 Bootloader 使用默认参数，从 A 区启动，App 5 秒后重新确认。

## 5. 说明

- 旧版记录里的“跳转 33 ms、64 KB 升级 7.8 s、CAN 位宽 2.02 μs”来自旧固件/旧适配器，**本版本未复测**，不再作为当前结论引用。
- OTA 耗时包含握手、擦除、传输、CRC、切区和复位，受固件大小、PCAN 驱动、USB 环境影响，单次数据仅供参考。
- 复测时请记录：固件大小、适配器型号、板子版本、代码 commit。
