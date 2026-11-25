# 验证记录

日期：2026-11-25  
环境：Windows + PCAN-USB + STM32F407VET6 + CH340 + ST-Link V2  
代码：FreeRTOS App 版本（commit `4f56dde`）

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

ST-Link 连接信息：

```
Target voltage = 3.3 V
Device ID: 0x413
Device flash Size: 512 Kbytes
Device family: STM32F405xx/F407xx/F415xx/F417xx
```

## 3. FreeRTOS 版 CAN OTA 实测

| 目标 | 固件大小 | 块数 | 耗时 | 结果 |
|---|---|---|---|---|
| App-A | 26648 字节 | 42 | 2.52 s | CRC 通过，切 A 区，启动确认成功 |
| App-B | 26648 字节 | 42 | 2.93 s | CRC 通过，切 B 区，启动确认成功 |

A/B 切换：

```text
A → B：成功，B 区启动确认
B → A：成功，A 区启动确认
```

OTA 主机输出：

```text
大小：26648 字节（补齐后）
0x34 已响应，节点最大块长度 642
写入 26648/26648 字节（100.0%）
0x37 结束传输
CRC 校验通过
结果: 刷写完成，等待复位  26648 字节 / 42 块 / 2.52 s
已切到 A 区并复位
```

串口启动日志：

```text
=== CAN Bootloader 启动 ===
[参数] 记录区=A 序号=... 启动区=A
[启动] 跳转到 App：0x08010000
[参数] 记录区=A 序号=...
=== 应用程序 V1.2.3 @ 0x08010000 ===
[应用] 向量表 = 0x08010000
[CAN] 500 kbps，接收 ID 0x7E0
[应用] UDS 就绪
[应用] 启动确认成功
```

B 区同理，地址为 `0x08040000`，向量表为 `0x08040000`。

## 4. 编译尺寸

```text
App-A / App-B:
Code    = 25568 B
RO-data = 1064 B
RW-data = 16 B
ZI-data = 43248 B
app.bin = 26648 B
```

Bootloader 保持裸机，约 30 KB，预算 32 KB。

## 5. 历史数据（裸机版本）

以下数据来自 **FreeRTOS 之前的裸机 App**，不再作为当前版本结论：

| 目标 | 固件大小 | 块数 | 耗时 |
|---|---|---|---|
| App-A | 21216 字节 | 34 | 2.29 s |
| App-B | 21216 字节 | 34 | 2.71 s |

## 6. 说明

- OTA 耗时包含握手、擦除、传输、CRC、切区和复位，受固件大小、PCAN 驱动、USB 环境影响，单次数据仅供参考。
- 当前版本仍未做：跳转耗时、CAN 中断延迟、任务栈高水位、堆剩余、长时间连续 OTA、OTA 断电/断线、连续 3 次未确认回滚的专项测试。
- 复测时请记录：固件大小、适配器型号、板子版本、代码 commit。
