# 固件（Bootloader + App）

两个独立的 Keil 工程，已在本机 **Keil MDK 5.43a + Arm Compiler 6.24（AC6）** 上编译通过（0 错 0 警告）。

## 目录

```
firmware/
├── bootloader/                 Keil 工程（裸机，链接 0x08000000 / 32 KB）
│   ├── Core/Inc|Src/           main、时钟、CAN、ISO-TP、UDS、Flash、参数区、启动决策
│   ├── Drivers/                CMSIS + STM32F4 HAL（从 DFP 包复制，自带，不依赖外部路径）
│   ├── MDK-ARM/
│   │   ├── Bootloader.uvprojx  直接双击打开
│   │   ├── startup_stm32f407xx.s
│   │   └── Objects/bootloader.hex
│   └── gen_project.py          重新生成 uvprojx（可选）
└── app/                        Keil 工程（链接 0x08010000 / 192 KB）
    ├── Core/Inc|Src/
    ├── Drivers/
    ├── MDK-ARM/App.uvprojx
    └── Objects/app.hex
```

## 编译结果

| 工程 | Code | RO-data | RW-data | 占用 Flash | 预算 | 占比 |
|---|---|---|---|---|---|---|
| Bootloader | 21,496 B | 1,148 B | 12 B | ≈ 22.1 KB | 32 KB | 69% |
| App A/B | 19,708 B | 1,100 B | 12 B | ≈ 20.3 KB | 192 KB | 10.6% |

命令行为：

```powershell
& 'D:\keil5\UV4\UV4.exe' -b 'MDK-ARM\Bootloader.uvprojx' -j0 -o build.log
`

> 注意：.uvprojx 必须是 **UTF-8 无 BOM**。用 PowerShell 的 Set-Content -Encoding UTF8 改过工程文件后 uVision 会静默失败（退出码 15、不产生日志）；用 Python 写文件时指定 encoding=utf-8 即可。

`powershell
```

## 下载地址

| 固件 | 起始地址 | 说明 |
|---|---|---|
| bootloader.hex（Objects/） | 0x08000000 | 只烧一次（除非改 Bootloader） |
| app.hex（Objects/） | 0x08010000 | 打开 App.uvprojx 编译产生 |
| app_b.hex（Objects_B/） | 0x08040000 | 打开 App_B.uvprojx 编译产生 |

> App 工程当前链接在 0x08010000（A 区）。要生成 B 区固件，把 `IROM1` 改成 0x08040000、大小 0x30000，并在 `main.c` 里把 `APP_START_ADDR` 改成 0x08040000，重新编译即可。

## 编译输出目录（两个工程分开，互不覆盖）

| 工程 | 输出目录 | 产物 |
|---|---|---|
| Bootloader.uvprojx | MDK-ARM/Objects/ | ootloader.hex |
| App.uvprojx | MDK-ARM/Objects/ | pp.hex（A 区） |
| App_B.uvprojx | MDK-ARM/Objects_B/ | pp_b.hex（B 区） |

irmware/prebuilt/ 是归档副本（ootloader.hex / pp_a.hex / pp_b.hex），可以直接烧，不依赖是否编译过。

## 已实现

**Bootloader**
- 时钟 168 MHz / APB1 42 MHz，CAN1 在 PB8/PB9（AF9）
- CAN 500 kbps（BRR 由 42 MHz/6/14 计算），过滤器只放行 0x7E0，环形接收队列
- ISO-TP 服务端：SF/FF/CF/FC、SN、BS/STmin、超时
- UDS：0x10 / 0x11 / 0x22 / 0x27 / 0x31 / 0x34 / 0x36 / 0x37 / 0x3E，含 0x78
- Flash：逐扇区擦除（每扇区喂狗）、字编程 + 回读校验、硬件 CRC32
- 参数区：双 slot、magic 后置、seq、CRC、UID 绑定
- 启动决策：KEY_BOOT、BKP 交接、双区回滚、跳转前复位外设/开 FPU/重映射 VTOR

**App**
- main 第一行设 `SCB->VTOR = 0x08010000`
- 5 s 后 `app_confirm_boot()`（清 boot_count、标记已确认）
- 运行期诊断：0x10 / 0x11 / 0x22 / 0x27 / 0x3E
- 收到 `10 02`：回正响应 → 写 BKP → 复位交权
- LED 心跳 + 每 2 s 打印一次 sinf（验证 FPU）

## 与上位机的约定（必须一致）

| 项 | 值 |
|---|---|
| 请求/响应 ID | 0x7E0 / 0x7E8 |
| 波特率 | 500 kbps |
| 擦除 | `31 01 FF00 + addr(4) + size(4)` |
| 校验 | `31 01 FF01 + addr(4) + size(4) + crc32(4)` |
| 切区 | `31 01 FF02 + area(1)` |
| 0x36 块长度 | 642（640 数据 + SID + BSC） |
| DID | F186 / F195 / FF00 / FF01 / FF02 |

## 联调顺序

1. 烧 `bootloader.hex`（0x08000000），串口应打印 `=== CAN Bootloader V1.0 ===`；
2. 烧 `app.hex`（0x08010000），复位后应打印 `[boot] jump to 0x08010000` 和 App 的版本行；
3. PC 接 USB-CAN，`python host/cli.py read --did 0xF195 --backend gs_usb`；
4. `python host/cli.py flash --bin firmware/demo.bin --addr 0x08040000 --area 1`；
5. 断电重启，串口应显示跳到 0x08040000，App 里 `22 FF00` 应返回 1。

## 已知限制

- 未在真实硬件上验证（板子还在路上），当前只保证「编译链接通过」；
- App 的 `s_unlocked` 只赋值未使用，编译器给 1 条警告，不影响功能；
- 教学版安全访问算法（异或 + 循环移位），真实项目应换 AES-CMAC + HSM；
- 断点续传依赖 DID FF01，断电后 `last_block` 会归零，续传逻辑需要上位机以节点返回值为准（当前实现如此）。

## 代码审查记录（V1.1）

修复的三个真问题（都会在真机上导致刷不进去）：

1. **ISO-TP 流控**：接收时回了 FC(BS=8)，但收满 8 帧后不再发第二个 FC，而主机是严格按 BS 等的——请求超过约 63 字节就会卡死（0x36 的 642 字节必挂）。改为 FC(BS=0)，一次收完。
2. **App 缺少 DID FF02**：上位机读不到 UID 折叠值，密钥按 uid=0 算，与板端必然对不上，连续 3 次后锁定。App 侧补上 FF02。
3. **交权后没有重新握手**：App 收到 10 02 会写 BKP 并复位，Bootloader 的会话/解锁状态是清零的，直接擦除会返回 NRC 0x33。上位机在交权后等待 0.8 s，再执行一次 10 03 → 27 → 10 02。

已知限制（不是 bug，但需要知道）：

- **断点续传实际无效**：擦除时 last_block 归零，FF01 永远读回 0；真正续传要把进度持久化到参数区。
- **0x34 只校验 4 字节对齐**，未强制长度是 4 的倍数（上位机已补齐，其他工具可能踩）。
- **启动不校验固件 CRC**，只看头部；B 区内容损坏但头部有效时仍会启动，可加启动 CRC（几毫秒）。
- 参数区在“待确认”状态下每次启动都会写一次（擦 16 KB），属于设计取舍。
