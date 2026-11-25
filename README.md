# STM32 CAN 双区 Bootloader 与 UDS OTA 升级

基于 STM32F407 的 CAN 总线双区 Bootloader：上位机通过 UDS over ISO-TP 把固件写入另一块应用区，支持 A/B 切换、启动 CRC 校验、连续 3 次未确认自动回滚。PC 端与板端各实现一遍协议，附假 ECU，没有硬件也能跑通全流程。

---

## 特性

- **A/B 双区**：写 B 区时 A 区不动，擦写中断电旧固件仍可启动
- **启动回滚**：切区后标记“待确认”，连续 3 次未确认自动切回旧区
- **启动 CRC**：跳转前用硬件 CRC 校验整片固件（192 KB 不到 1 ms）
- **掉电原子参数区**：双 slot 交替写，magic 后置 + 结构 CRC + UID 绑定
- **完整 UDS**：0x10 / 0x11 / 0x22 / 0x27 / 0x31 / 0x34 / 0x36 / 0x37 / 0x3E，含 0x78 挂起处理
- **分层上位机**：CLI → 刷写流程 → UDS → ISO-TP → 驱动，每层可单独测试
- **无硬件可跑**：virtual 总线 + 假 ECU，15 个单元测试

## 架构

```
PC 端（Python）                         板端（C）
  cli.py                                    Bootloader
   │ 参数解析                                 main（启动决策）
   ▼                                          ├─ uds.c    9 项服务
  flasher.py  流程编排                        ├─ isotp.c  传输层
   ▼                                          └─ bsp_can.c 驱动
  uds.py      报文                        旁挂：flash_if / params / boot
   ▼
  isotp.py    分帧                          App（FreeRTOS）
   ▼                                          main（VTOR / 外设 / 启动任务）
  can_driver.py  设备                         ├─ app_tasks.c UDS/应用/看门狗任务
                                               └─ app_uds.c  运行期诊断
```

## App 侧 FreeRTOS

Bootloader 保持裸机（32 KB 预算，启动确定性优先）；**只有 App 使用 FreeRTOS V10.3.1**。

| 任务 | 优先级 | 栈（word） | 职责 |
|---|---:|---:|---|
| `uds` | 4 | 512 | `app_uds_poll()`：收 ISO-TP、解析 UDS、响应 |
| `app` | 3 | 256 | 5 s 启动确认、LED、业务心跳 |
| `wdg` | 2 | 128 | 检查两个业务任务心跳，正常才喂 IWDG |

CAN 接收改为中断 + 队列：`HAL_CAN_RxFifo0MsgPendingCallback()` 里 `xQueueSendFromISR()`，`bsp_can_recv()` 用 `xQueueReceive()` 阻塞等待，不再忙等 FIFO。

> `App` 的 CAN 中断优先级从 1 改为 5，满足 FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`。


## 目录结构

```
.
├── host/                  PC 端
│   ├── can_driver.py      适配器封装：gs_usb / pcan / slcan / virtual
│   ├── isotp.py           ISO-TP 收发（SF/FF/CF/FC、SN、BS/STmin）
│   ├── uds.py             UDS 客户端（9 项服务、NRC、0x78）
│   ├── crc_hw.py          STM32 硬件 CRC32（非反射，与 zlib 不同）
│   ├── flasher.py         刷写流程编排
│   ├── fake_ecu.py        假 ECU（无硬件时模拟节点）
│   └── cli.py             命令行入口
├── firmware/
│   ├── bootloader/        Bootloader 工程（0x08000000 / 32 KB）
│   │   ├── Core/          main / bsp_can / isotp / uds / flash_if / params / boot
│   │   └── MDK-ARM/       Keil 工程
│   ├── app/               App 工程（A: 0x08010000 / B: 0x08040000）
│   │   ├── Core/          main / app_uds / app_boot / app_tasks
│   │   ├── Middlewares/   FreeRTOS V10.3.1（tasks/queue/list + Cortex-M4F port + heap_4）
│   │   └── MDK-ARM/       App.uvprojx（A 区）、App_B.uvprojx（B 区）
│   └── demo.bin           小测试固件（demo 用）
├── tests/                 CRC / ISO-TP / 端到端
├── docs/verification.md   编译、测试与实测数据
├── requirements.txt
└── README.md
```

> `MDK-ARM/Objects*` 只保留可直接烧录的 `.hex` / `.bin` 和 `.sct`；
> 中间文件 `.o/.axf/.map` 已清理，Keil 重新编译会自动生成。

## 快速开始

### 1. 上位机（不需要硬件）

```bash
python -m venv .venv
.venv\Scripts\activate        # Windows
pip install -r requirements.txt

# 假 ECU 完整刷一遍（擦除 → 下载 → 校验 → 切区 → 复位）
python host/cli.py flash --bin firmware/demo.bin --addr 0x08040000 --fake --area 1

# 切换启动区
python host/cli.py rollback --to a --fake
```

### 2. 跑测试

```bash
python -m unittest discover -s tests -v
```

### 3. 固件编译（Keil MDK + Arm Compiler 6）

| 工程 | 链接地址 | 产物 |
|---|---|---|
| `firmware/bootloader/MDK-ARM/Bootloader.uvprojx` | 0x08000000 | `bootloader.hex` |
| `firmware/app/MDK-ARM/App.uvprojx` | 0x08010000 | `app.hex` |
| `firmware/app/MDK-ARM/App_B.uvprojx` | 0x08040000 | `app_b.hex` |

三个工程均 0 error / 0 warning。Bootloader 约 30 KB（预算 32 KB），App 约 26 KB（预算 192 KB）。

### 4. 接真硬件

```bash
# candleLight / gs_usb
python host/cli.py flash --bin firmware/app/MDK-ARM/Objects_B/app_b.bin --addr 0x08040000 --backend gs_usb --area 1
```

烧录顺序：Bootloader → 0x08000000，App-A → 0x08010000，App-B → 0x08040000。

> 直接烧 App hex 不会更新参数区里的 CRC，Bootloader 会判 CRC 失败。整片恢复请用 `flash_all.bat`（先全片擦除，再从 A 区启动）；只升级 App 请用 CAN OTA。

## 协议约定

| 项 | 值 |
|---|---|
| 请求 / 响应 ID | 0x7E0 / 0x7E8（标准帧） |
| 波特率 | 500 kbps（42 MHz ÷ 6 ÷ 14，采样点 85.71%） |
| 擦除 | `31 01 FF00 + addr(4) + size(4)` |
| 校验 | `31 01 FF01 + addr(4) + size(4) + crc32(4)` |
| 切区 | `31 01 FF02 + area(1)` |
| 0x36 块长 | 642（640 数据 + SID + BSC） |
| DID | F186 会话 / F195 版本 / FF00 当前区 / FF01 最后块号 / FF02 UID 折叠 |
| CRC | 多项式 0x04C11DB7，初值 0xFFFFFFFF，MSB-first，无终值异或 |

## Flash 分区（F407VET6，512 KB）

| 区域 | 地址 | 大小 |
|---|---|---|
| Bootloader | 0x08000000 | 32 KB |
| 参数 A / B | 0x08008000 / 0x0800C000 | 16 KB × 2 |
| App-A | 0x08010000 | 192 KB |
| App-B | 0x08040000 | 192 KB（物理 256 KB） |

## 状态

| 项 | 数据 |
|---|---|
| 上位机 + 假 ECU | 15 个测试通过（约 1.35 s） |
| Bootloader 编译 | 约 30 KB，0 error 0 warning |
| App-A / App-B 编译 | 约 26 KB，0 error 0 warning（含 FreeRTOS） |
| FreeRTOS App 镜像 | 26536 字节 / 42 块（待上板复测 OTA 时间） |
| 上板 OTA（裸机历史数据） | App-A 21216 字节 / 34 块 / 约 2.3 s；App-B 21216 字节 / 34 块 / 约 2.7 s |
| 启动确认 | App 启动 5 秒后打印 `[应用] 启动确认成功` |

测量条件与明细见 [docs/verification.md](docs/verification.md)。

## 已知限制

- 断点续传未持久化：擦除会清零进度，`--resume` 目前等价于从头写
- 安全访问是教学算法（异或 + 循环移位），量产应换 AES-CMAC + HSM
- 没有固件签名，只防随机损坏，不防恶意篡改
- 参数区每次升级会擦写若干次，寿命约 2000 次升级

## 文档

- `docs/verification.md`：验证记录与上板实测数据
- `docs/freertos.md`：App 侧 FreeRTOS 任务、CAN 队列、中断向量、参数与构建说明

## License

MIT
