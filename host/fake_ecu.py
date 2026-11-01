# -*- coding: utf-8 -*-
"""假 ECU：在虚拟总线上模拟一个实现了 UDS 刷写的节点。

用途：没有硬件时，也能把上位机、ISO-TP、UDS 全流程跑通。

    from fake_ecu import FakeEcu
    import can, time
    bus_pc = can.Bus(interface="virtual", channel="vcan0")
    bus_ecu = can.Bus(interface="virtual", channel="vcan0")
    ecu = FakeEcu(bus_ecu); ecu.start()

    # 之后正常用 UdsClient / Flasher 操作 bus_pc
"""
from __future__ import annotations

import threading
import time
from typing import Optional

import can

from crc_hw import crc32_stm32_fast
from isotp import IsoTp, IsoTpError, RxTimeout
from uds import DID_ACTIVE_AREA, DID_LAST_BLOCK, DID_UID_FOLD, DID_VERSION, teaching_key


NRC = {
    "general": 0x10,
    "service": 0x11,
    "subfunc": 0x12,
    "length": 0x13,
    "conditions": 0x22,
    "sequence": 0x24,
    "range": 0x31,
    "security": 0x33,
    "invalid_key": 0x35,
    "attempts": 0x36,
    "delay": 0x37,
    "programming": 0x72,
}


class FakeEcu(threading.Thread):
    def __init__(self, bus: can.BusABC, uid_fold: int = 0x12345678,
                 version: bytes = b"\x01\x02\x03\x00",
                 max_block_length: int = 642,
                 verbose: bool = True) -> None:
        super().__init__(daemon=True)
        self.bus = bus
        self.tp = IsoTp(bus, tx_id=0x7E8, rx_id=0x7E0)
        self.uid_fold = uid_fold
        self.version = version
        self.max_block_length = max_block_length
        self.verbose = verbose

        self.session = 0x01
        self.unlocked = False
        self.area = 0
        self.seed = 0x11223344
        self.wrong_key_count = 0

        self.pending_addr: Optional[int] = None
        self.pending_size = 0
        self.expect_bsc = 1
        self.last_block = 0
        self.write_offset = 0
        self.region = bytearray()
        self.crc_expected: Optional[int] = None

        self._stop = threading.Event()

    # ------------------------------------------------------------ 线程
    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                req = self.tp.recv(timeout_ms=100)
            except RxTimeout:
                continue
            except IsoTpError as exc:
                if self.verbose:
                    print(f"[ecu] ISO-TP 错误：{exc}")
                continue
            try:
                resp = self.handle(req)
            except Exception as exc:                      # noqa: BLE001
                if self.verbose:
                    print(f"[ecu] 处理异常：{exc}")
                resp = bytes([0x7F, req[0], NRC["general"]]) if req else None
            if resp:
                if self.verbose:
                    def _short(b: bytes) -> str:
                        text = b.hex(" ")
                        return text if len(text) <= 60 else text[:57] + "..."
                    print(f"[ecu] {_short(req)} -> {_short(resp)}")
                self.tp.send(resp)

    # ------------------------------------------------------------ 分发
    def handle(self, req: bytes) -> Optional[bytes]:
        sid = req[0]
        if sid == 0x10:
            return self._session(req)
        if sid == 0x11:
            return bytes([0x51, req[1] if len(req) > 1 else 0x01])
        if sid == 0x22:
            return self._read_data(req)
        if sid == 0x27:
            return self._security(req)
        if sid == 0x31:
            return self._routine(req)
        if sid == 0x34:
            return self._download(req)
        if sid == 0x36:
            return self._transfer(req)
        if sid == 0x37:
            return bytes([0x77])
        if sid == 0x3E:
            return bytes([0x7E, 0x00])
        return bytes([0x7F, sid, NRC["service"]])

    # ------------------------------------------------------------ 各服务
    def _session(self, req: bytes) -> bytes:
        if len(req) < 2:
            return bytes([0x7F, 0x10, NRC["length"]])
        session = req[1]
        if session not in (0x01, 0x02, 0x03):
            return bytes([0x7F, 0x10, NRC["subfunc"]])
        if session == 0x02 and not self.unlocked:
            return bytes([0x7F, 0x10, NRC["security"]])
        self.session = session
        return bytes([0x50, session, 0x00, 0x32, 0x01, 0xF4])

    def _read_data(self, req: bytes) -> bytes:
        if len(req) < 3:
            return bytes([0x7F, 0x22, NRC["length"]])
        did = (req[1] << 8) | req[2]
        if did == DID_VERSION:
            data = self.version
        elif did == 0xF190:
            data = b"LSVAA1234567890AB"
        elif did == DID_ACTIVE_AREA:
            data = bytes([self.area])
        elif did == DID_LAST_BLOCK:
            data = self.last_block.to_bytes(2, "big")
        elif did == DID_UID_FOLD:
            data = self.uid_fold.to_bytes(4, "big")
        else:
            return bytes([0x7F, 0x22, NRC["range"]])
        return bytes([0x62, req[1], req[2]]) + data

    def _security(self, req: bytes) -> bytes:
        if len(req) < 2:
            return bytes([0x7F, 0x27, NRC["length"]])
        sub = req[1]
        if sub == 0x01:
            return bytes([0x67, 0x01]) + self.seed.to_bytes(4, "big")
        if sub == 0x02:
            if len(req) < 6:
                return bytes([0x7F, 0x27, NRC["length"]])
            key = int.from_bytes(req[2:6], "big")
            if key == teaching_key(self.seed, self.uid_fold):
                self.unlocked = True
                return bytes([0x67, 0x02])
            self.wrong_key_count += 1
            if self.wrong_key_count >= 3:
                return bytes([0x7F, 0x27, NRC["attempts"]])
            return bytes([0x7F, 0x27, NRC["invalid_key"]])
        return bytes([0x7F, 0x27, NRC["subfunc"]])

    def _routine(self, req: bytes) -> bytes:
        if len(req) < 4:
            return bytes([0x7F, 0x31, NRC["length"]])
        if not self.unlocked:
            return bytes([0x7F, 0x31, NRC["security"]])
        rid = (req[2] << 8) | req[3]
        if rid == 0xFF00:                              # 擦除
            addr = int.from_bytes(req[4:8], "big")
            size = int.from_bytes(req[8:12], "big")
            self.region = bytearray(b"\xFF" * size)
            self.pending_addr = addr
            self.pending_size = size
            self.last_block = 0
            self.write_offset = 0
            self.crc_expected = None
            return bytes([0x71, 0x01, 0xFF, 0x00, 0x00])
        if rid == 0xFF01:                              # 校验
            addr = int.from_bytes(req[4:8], "big")
            size = int.from_bytes(req[8:12], "big")
            expected = int.from_bytes(req[12:16], "big") if len(req) >= 16 else None
            actual = crc32_stm32_fast(bytes(self.region[:size]))
            if expected is not None and expected != actual:
                return bytes([0x7F, 0x31, NRC["programming"]])
            return bytes([0x71, 0x01, 0xFF, 0x01, 0x00])
        if rid == 0xFF02:                              # 切区
            if len(req) < 5:
                return bytes([0x7F, 0x31, NRC["length"]])
            self.area = req[4] & 0x01
            return bytes([0x71, 0x01, 0xFF, 0x02, 0x00])
        return bytes([0x7F, 0x31, NRC["range"]])

    def _download(self, req: bytes) -> bytes:
        if len(req) < 11:
            return bytes([0x7F, 0x34, NRC["length"]])
        if not self.unlocked:
            return bytes([0x7F, 0x34, NRC["security"]])
        addr = int.from_bytes(req[4:8], "big")
        size = int.from_bytes(req[8:12], "big")
        self.pending_addr = addr
        self.pending_size = size
        if len(self.region) < size:
            self.region = bytearray(b"\xFF" * size)
        self.expect_bsc = 1
        self.write_offset = 0
        return bytes([0x74, 0x20, (self.max_block_length >> 8) & 0xFF,
                      self.max_block_length & 0xFF])

    def _transfer(self, req: bytes) -> bytes:
        if len(req) < 2:
            return bytes([0x7F, 0x36, NRC["length"]])
        if self.pending_addr is None:
            return bytes([0x7F, 0x36, NRC["sequence"]])
        bsc = req[1]
        if bsc != (self.expect_bsc & 0xFF):
            return bytes([0x7F, 0x36, 0x73])
        data = req[2:]
        offset = self.write_offset
        self.region[offset:offset + len(data)] = data
        self.write_offset += len(data)
        self.last_block += 1
        self.expect_bsc += 1
        return bytes([0x76, bsc])
