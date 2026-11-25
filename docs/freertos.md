# App 侧 FreeRTOS 说明

## 范围

- Bootloader：保持裸机。32 KB 分区、启动确定性优先，不引入调度器。
- App：FreeRTOS V10.3.1，Cortex-M4F，heap_4，tick 1000 Hz。

## 任务

| 任务 | 优先级 | 栈（word） | 职责 |
|---|---:|---:|---|
| `uds` | 4 | 1024 | `app_uds_poll()`，收 ISO-TP、解析 UDS、响应 |
| `app` | 3 | 512 | 5 s 启动确认、LED、业务心跳 |
| `wdg` | 2 | 256 | 检查两个业务任务心跳，正常才喂 IWDG |

## CAN 接收

FreeRTOS 版本改为中断 + 队列：

```text
CAN1_RX0_IRQHandler
  -> HAL_CAN_IRQHandler
    -> HAL_CAN_RxFifo0MsgPendingCallback
      -> xQueueSendFromISR(s_rx_queue)
        -> bsp_can_recv()  xQueueReceive()
```

- `CAN_IT_RX_FIFO0_MSG_PENDING` 已使能；
- CAN 中断优先级 5，满足 `configMAX_SYSCALL_INTERRUPT_PRIORITY=5`；
- 队列长度 32，满了丢新帧并计数。

## 中断向量

- `SVC_Handler` / `PendSV_Handler` 由 FreeRTOS port 直接提供（宏映射），不经过 C 包装；
- `SysTick_Handler` 同时调用 `HAL_IncTick()` 和 `xPortSysTickHandler()`；
- `HAL_Init()` 仍然使用 SysTick 做 HAL 时基，调度器启动后由 FreeRTOS 接管。

## 看门狗

- App IWDG 约 2 s；
- `wdg` 任务每 500 ms 检查 `uds` / `app` 心跳；
- 两个心跳都有推进才喂狗，否则等复位。

## 参数

| 宏 | 值 |
|---|---|
| `configTOTAL_HEAP_SIZE` | 32 KB |
| `configTICK_RATE_HZ` | 1000 |
| `configMAX_PRIORITIES` | 7 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 |

## 构建

- `App.uvprojx` / `App_B.uvprojx` 已加入 FreeRTOS 组：
  - `tasks.c`、`queue.c`、`list.c`
  - `portable/GCC/ARM_CM4F/port.c`
  - `portable/MemMang/heap_4.c`
- include 路径增加：
  - `Middlewares/FreeRTOS/Source/include`
  - `Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM4F`
- 新增 `Core/Src/app_tasks.c`、`Core/Inc/app_tasks.h`、`Core/Inc/FreeRTOSConfig.h`。

## 已验证

- App / App_B Keil 构建：0 error / 0 warning；
- Code 25568 B 左右，RO 1064 B，RW 16 B，ZI 43248 B；
- `app.bin` / `app_b.bin` 26648 B；
- 15 个 host 测试通过；
- 上板：App 启动 5 秒确认成功；
- 上板：`can_probe` 收到 `0x7E8` 响应；
- 上板：A 区 OTA 26648 B / 42 块 / 2.52 s；
- 上板：B 区 OTA 26648 B / 42 块 / 2.93 s；
- 上板：OTA 后 B 区启动确认、切回 A 区启动确认均通过。

## 说明

- `app_tasks.c` / `bsp_can.c` 里保留了 `g_uds_loops`、`g_can_isr_count` 等调试计数，便于后续观察任务和队列；
- 如果量产，可移除这些调试变量。
