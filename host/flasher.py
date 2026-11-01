# -*- coding: utf-8 -*-
"""固件刷写流程：把一次 UDS 升级的 26 个步骤封装成可调用的类。

与项目设计说明书 7.5 节「完整升级时序」一一对应：
    10 03 → 27 01/02 → 10 02 → 31 01 FF00（擦除）→ 34 → 36*N → 37
    → 31 01 FF01（校验）→ 31 01 FF02（切区）→ 11 01（复位）
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from dataclasses import dataclass
from typing import Optional

from crc_hw import crc32_stm32_fast, pad_to_word
from uds import UdsClient, NrcError, DID_VERSION


DEFAULT_BLOCK_LENGTH = 642       # 含 SID + 块序号；即每块 640 字节数据


@dataclass
class FlashResult:
    ok: bool
    firmware_size: int
    elapsed_s: float
    blocks: int
    message: str = ""


class Flasher:
    def __init__(self, uds: UdsClient, block_length: int = DEFAULT_BLOCK_LENGTH,
                 progress_file: Optional[str] = None, verbose: bool = True) -> None:
        self.uds = uds
        self.block_length = block_length
        self.data_per_block = block_length - 2
        self.progress_file = progress_file
        self.verbose = verbose

    # ------------------------------------------------------------ 入口
    def flash(self, bin_path: str, addr: int, size: Optional[int] = None,
              resume: bool = False, uid_fold: int = 0) -> FlashResult:
        t0 = time.perf_counter()
        with open(bin_path, "rb") as fp:
            firmware = fp.read()
        firmware = pad_to_word(firmware)
        total = size or len(firmware)
        if total > len(firmware):
            raise ValueError("指定大小超过固件长度")

        crc = crc32_stm32_fast(firmware[:total])
        if self.verbose:
            print(f"固件：{bin_path}")
            print(f"大小：{len(firmware)} 字节（补齐后），CRC32=0x{crc:08X}")

        self.uds.enter_programming(uid_fold=uid_fold)
        # App 收到 10 02 后会写 BKP 并复位，Bootloader 的会话/解锁状态是清零的。
        # 因此等它重启完成，再和 Bootloader 握手一次。
        time.sleep(0.8)
        self.uds.enter_programming(uid_fold=uid_fold)
        if self.verbose:
            print("已进入编程会话（Bootloader）")

        # 擦除：可能触发 0x78，UdsClient 已处理
        self.uds.erase_region(addr, total)
        if self.verbose:
            print(f"擦除完成：0x{addr:08X} + {total}")

        max_block = self.uds.request_download(addr, total)
        if self.verbose:
            print(f"0x34 已响应，节点最大块长度 {max_block}")
        block_len = min(self.block_length, max_block)
        data_per_block = block_len - 2

        start_block = 1
        if resume:
            try:
                last = self.uds.read_last_block()
                if last:
                    start_block = last + 1
                    if self.verbose:
                        print(f"断点续传：从第 {start_block} 块继续")
            except NrcError:
                pass

        blocks = 0
        bsc = start_block & 0xFF
        offset = (start_block - 1) * data_per_block
        while offset < total:
            chunk = firmware[offset:offset + data_per_block]
            self.uds.transfer_data(bsc, chunk)
            offset += len(chunk)
            bsc = (bsc + 1) & 0xFF
            blocks += 1
            if self.verbose and (blocks % 16 == 0 or offset >= total):
                pct = min(100.0, offset * 100.0 / total)
                print(f"  写入 {offset}/{total} 字节（{pct:5.1f}%）")

        self.uds.request_transfer_exit()
        if self.verbose:
            print("0x37 结束传输")

        self.uds.check_crc(addr, total, crc)
        if self.verbose:
            print("CRC 校验通过")

        elapsed = time.perf_counter() - t0
        return FlashResult(True, total, elapsed, blocks, "刷写完成，等待复位")

    # ------------------------------------------------------------ 辅助
    def switch_and_reset(self, area: int) -> None:
        self.uds.switch_area(area)
        if self.verbose:
            print(f"已切到 App-{'A' if area == 0 else 'B'}，准备复位")
        self.uds.ecu_reset(0x01)

    @staticmethod
    def file_hash(path: str) -> str:
        h = hashlib.sha256()
        with open(path, "rb") as fp:
            for chunk in iter(lambda: fp.read(4096), b""):
                h.update(chunk)
        return h.hexdigest()[:16]

    def save_progress(self, path: str, addr: int, blocks: int) -> None:
        if not self.progress_file:
            return
        data = {"firmware": os.path.basename(path), "addr": addr, "blocks": blocks}
        with open(self.progress_file, "w", encoding="utf-8") as fp:
            json.dump(data, fp, ensure_ascii=False, indent=2)


class DryRunUds:
    """不接硬件时的假 UDS：把收到的请求打印出来，供调试脚本用。"""

    def __init__(self) -> None:
        self.calls: list[bytes] = []

    def __getattr__(self, name):
        def _fake(*args, **kwargs):
            self.calls.append(name.encode())
            if name == "request_download":
                return DEFAULT_BLOCK_LENGTH
            if name == "read_last_block":
                return 0
            return b""
        return _fake
