# -*- coding: utf-8 -*-
"""与 STM32F407 硬件 CRC 单元一致的 CRC32 实现。

F407 的 CRC 单元特性（RM0090）：
    * 多项式 0x04C11DB7（非反射，MSB-first）
    * 初始值 0xFFFFFFFF
    * 无最终异或
    * 以 32 位字为单位累算

注意：Python 标准库 zlib.crc32 是反射（LSB-first）变体，结果与硬件完全不同。
用 zlib 去校验 STM32 算出来的值，会得到“逐字节一致但 CRC 不符”的经典怪现象。
"""
from __future__ import annotations

POLY = 0x04C11DB7
INIT = 0xFFFFFFFF


def _make_table() -> list[int]:
    table = []
    for i in range(256):
        crc = i << 24
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) & 0xFFFFFFFF if crc & 0x80000000 \
                else (crc << 1) & 0xFFFFFFFF
        table.append(crc)
    return table


_TABLE = _make_table()


def crc32_stm32(data: bytes, init: int = INIT) -> int:
    """逐字节的 MSB-first 实现，结果与硬件一致（长度需为 4 的倍数）。"""
    crc = init & 0xFFFFFFFF
    for byte in data:
        crc ^= (byte << 24) & 0xFFFFFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) & 0xFFFFFFFF if crc & 0x80000000 \
                else (crc << 1) & 0xFFFFFFFF
    return crc


def crc32_stm32_fast(data: bytes, init: int = INIT) -> int:
    """查表实现，快约 8 倍，结果与 crc32_stm32 相同。"""
    crc = init & 0xFFFFFFFF
    for byte in data:
        index = ((crc >> 24) ^ byte) & 0xFF
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _TABLE[index]
    return crc


def pad_to_word(data: bytes, fill: int = 0xFF) -> bytes:
    """把固件补齐到 4 字节倍数；板端按同一长度计算 CRC。"""
    rem = len(data) % 4
    if rem:
        data = data + bytes([fill] * (4 - rem))
    return data


if __name__ == "__main__":
    # 自检：两个实现必须一致，且与 zlib 必须不同（变体不同）
    import zlib
    sample = b"123456789"
    a, b = crc32_stm32(sample), crc32_stm32_fast(sample)
    print(f"bitwise: 0x{a:08X}")
    print(f"table  : 0x{b:08X}")
    print(f"zlib   : 0x{zlib.crc32(sample):08X}")
    assert a == b
    assert a != zlib.crc32(sample)
    print("self-check OK")
