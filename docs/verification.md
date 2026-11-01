# 验证记录

只记录**已经跑过、可复现**的结果。编译、单元测试、端到端与上板实测数据均标注测量条件。

---

## 1. 编译

工具链：Keil MDK 5.43a + Arm Compiler 6.24（AC6），优化 Level 1。

| 工程 | Code | RO-data | RW-data | 合计 | 预算 | 结果 |
|---|---|---|---|---|---|---|
| Bootloader | 21,496 B | 1,148 B | 12 B | 22.4 KB | 32 KB（70%） | 0 error / 0 warning |
| App-A | 19,708 B | 1,100 B | 12 B | 20.3 KB | 192 KB（10.6%） | 0 error / 0 warning |
| App-B | 19,708 B | 1,100 B | 12 B | 20.3 KB | 192 KB（10.6%） | 0 error / 0 warning |

## 2. 单元测试（15 个）

```bash
python tests/test_crc.py      # 4 个：两实现一致、与 zlib 不同、补位、空数据
python tests/test_isotp.py    # 6 个：1/7/8/200/4095 字节、超时
python tests/test_flash.py    # 5 个：读版本、会话+解锁、切区、2000 字节刷写、错误 CRC 被拒
```

全部通过

## 3. 虚拟总线端到端

```bash
python host/cli.py flash --bin firmware/demo.bin --addr 0x08040000 --fake --area 1
```

结果：3000 字节 / 5 块 / 0.81 s，擦除 → 0x34 → 5 次 0x36 → 0x37 → CRC 校验 → 切区 → 复位，全流程通过。

`0.81 s` 含交权后固定等待的 0.8 s，实际协议交互约 10 ms 量级。

## 4. 上板实测

**测试条件**

- 目标板：STM32F407VET6 最小系统板 ×2
- 收发器：SN65HVD230 ×2（板载 120 Ω）
- 适配器：USB-CAN（gs_usb）
- 固件：Bootloader 22.4 KB，App 20.3 KB；升级镜像 64 KB
- 代码版本：`e0d13dd`
- 日期：2026-10-30

| # | 项 | 实测 | 方法 | 结论 |
|---|---|---|---|---|
| 1 | Bootloader 跳转时间 | **33 ms** | 逻辑分析仪：上电沿 → App 首帧串口 | ✅ |
| 2 | 64 KB 升级耗时 | **7.8 s** | 上位机计时：擦除 → CRC 通过 | ✅ |
| 3 | 启动 CRC 耗时 | **0.5 ms** | 逻辑分析仪 | ✅ |
| 4 | CAN 位宽 | **2.02 μs** | 逻辑分析仪 | ✅ |
| 5 | 总线电阻（断电） | **60.2 Ω** | 万用表量 CANH-CANL | ✅ |
| 6 | 回滚触发 | **第 3 次** | `APP_SKIP_CONFIRM` 固件，串口看 `boot_count` | ✅ |

> 以上为单次测量，未做多次平均；需要时补 3 次均值与波动范围。
> 位宽 2.02 μs 对应约 495 kbps。

### 记录规则

- 每项测 3 次，报 均值（最小到最大）
- 必须记录：固件大小、适配器型号、起止点定义
- 未达标也记录，写清现象与初步分析
- 单位统一：ms / s / μs / Ω
- 每次实测附代码 commit，保证可追溯

## 5. 复现步骤

```bash
# 1) 环境
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt

# 2) 无硬件跑通全流程
python host/cli.py flash --bin firmware/demo.bin --addr 0x08040000 --fake --area 1

# 3) 测试
python tests/test_crc.py
python tests/test_isotp.py
python tests/test_flash.py

# 4) 接真硬件
python host/cli.py flash --bin firmware/app/app_b.hex --addr 0x08040000 --backend gs_usb --area 1
```
