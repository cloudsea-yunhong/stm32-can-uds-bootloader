# 预编译固件

这些是当前 FreeRTOS 版本的可烧录产物，按 `SHA256SUMS.txt` 校验。

| 文件 | 烧录地址 | 说明 |
|---|---|---|
| `bootloader.hex` | `0x08000000` | 裸机 Bootloader |
| `app_a.hex` / `app_a.bin` | `0x08010000` | App-A，FreeRTOS |
| `app_b.hex` / `app_b.bin` | `0x08040000` | App-B，FreeRTOS |

## 烧录

整片恢复：

```text
flash_all.bat
```

它先全片擦除，再烧 Bootloader、App-A、App-B。

只做 CAN OTA：

```text
flash_app_a.bat
flash_app_b.bat
```

## 注意

- 直接烧 `app_a.hex` / `app_b.hex` 不会更新参数区里的 CRC，Bootloader 可能拒绝跳转。
- 整片恢复用 `flash_all.bat`；只升级 App 用 CAN OTA。
- 当前 App 镜像大小 **26648 B**，0x36 传输为 **42 块**。
- 源码 commit 见仓库根目录 `README.md` 的“状态”一节。
