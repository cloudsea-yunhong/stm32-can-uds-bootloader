# -*- coding: utf-8 -*-
"""ISO-TP（ISO 15765-2）传输层：把 8 字节的 CAN 帧拼成任意长度的报文。

实现要点（与项目设计说明书第 5 章对应）：
  * SF/FF/CF/FC 四类帧；
  * 首帧 12 位长度，单包上限 4095 字节；
  * 连续帧序号 SN 从 1 开始，0~15 循环；
  * 流控 BS（块大小）与 STmin（最小间隔）；
  * 接收超时 N_Cr、发送等流控超时 N_Bs。
"""
from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Optional

import can


# ---------------------------------------------------------------- 异常
class IsoTpError(Exception):
    """ISO-TP 层错误基类。"""


class RxTimeout(IsoTpError):
    pass


class TxTimeout(IsoTpError):
    pass


class FlowControlOverflow(IsoTpError):
    pass


class SequenceError(IsoTpError):
    pass


class FrameFormatError(IsoTpError):
    pass


@dataclass
class FlowControl:
    block_size: int          # 0 表示后续连续帧一次性发完
    stmin_ms: float          # 已换算成毫秒


class IsoTp:
    """单通道 ISO-TP 收发器（阻塞式，够上位机用）。

    参数：
        bus       : can.BusABC
        tx_id     : 发送用 CAN ID（诊断请求 0x7E0）
        rx_id     : 接收用 CAN ID（诊断响应 0x7E8）
        pad       : 帧尾填充字节（经典 CAN 不足 8 字节时的填充值）
        n_bs_ms   : 发送方等待流控帧的超时
        n_cr_ms   : 接收方等待连续帧的超时
    """

    def __init__(self, bus: can.BusABC, tx_id: int = 0x7E0, rx_id: int = 0x7E8,
                 pad: int = 0x00, n_bs_ms: float = 1000.0,
                 n_cr_ms: float = 1000.0, verbose: bool = False) -> None:
        self.bus = bus
        self.tx_id = tx_id
        self.rx_id = rx_id
        self.pad = pad & 0xFF
        self.n_bs = n_bs_ms / 1000.0
        self.n_cr = n_cr_ms / 1000.0
        self.verbose = verbose

    # ------------------------------------------------------------ 发送
    def send(self, payload: bytes) -> None:
        """发送一条完整报文（自动选择单帧/多帧）。"""
        data = bytes(payload)
        if not data:
            raise ValueError("空报文")

        if len(data) <= 7:                      # 单帧 SF
            self._send_frame(bytes([len(data)]) + data)
            return

        total = len(data)
        if total > 4095:
            raise ValueError("ISO-TP 单包上限 4095 字节")

        # 首帧 FF：0x1L + 长度低字节 + 前 6 字节数据
        ff = bytes([0x10 | (total >> 8), total & 0xFF]) + data[:6]
        self._send_frame(ff)
        if self.verbose:
            print(f"  [isotp] FF len={total}")

        fc = self._wait_flow_control()
        idx, sn = 6, 1
        in_block = 0
        while idx < total:
            chunk = data[idx:idx + 7]
            self._send_frame(bytes([0x20 | (sn & 0x0F)]) + chunk)
            if self.verbose:
                print(f"  [isotp] CF sn={sn & 0x0F} len={len(chunk)}")
            idx += len(chunk)
            sn = (sn + 1) & 0x0F
            in_block += 1

            if idx < total:
                self._sleep(stmin_ms=fc.stmin_ms)
                if fc.block_size and in_block >= fc.block_size:
                    fc = self._wait_flow_control()
                    in_block = 0

    def _wait_flow_control(self) -> FlowControl:
        deadline = time.monotonic() + self.n_bs
        waits = 0
        while time.monotonic() < deadline:
            frame = self._recv_frame(deadline)
            if frame is None:
                break
            pci = frame[0]
            if (pci & 0xF0) != 0x30:
                continue
            fs = pci & 0x0F
            if fs == 0:                          # CTS
                bs = frame[1]
                stmin = frame[2]
                return FlowControl(block_size=bs, stmin_ms=self._stmin_to_ms(stmin))
            if fs == 1:                          # WAIT
                waits += 1
                if waits > 3:
                    raise TxTimeout("流控帧连续 WAIT 超过 3 次")
                deadline = time.monotonic() + self.n_bs
                continue
            if fs == 2:
                raise FlowControlOverflow("接收方缓冲不足（FC=OVFLW）")
        raise TxTimeout("等待流控帧超时（N_Bs）")

    @staticmethod
    def _stmin_to_ms(stmin: int) -> float:
        if 0x00 <= stmin <= 0x7F:
            return float(stmin)
        if 0xF1 <= stmin <= 0xF9:
            # 100~900 微秒：Python 的 sleep 精度约 1 ms，按 1 ms 处理
            return 1.0
        return 127.0                             # 保留值，按标准取 127 ms

    def _sleep(self, stmin_ms: float) -> None:
        if stmin_ms > 0:
            time.sleep(stmin_ms / 1000.0)

    def _send_frame(self, frame: bytes) -> None:
        data = bytes(frame)
        if len(data) < 8:
            data = data + bytes([self.pad] * (8 - len(data)))
        msg = can.Message(arbitration_id=self.tx_id, data=data,
                          is_extended_id=False)
        self.bus.send(msg)

    # ------------------------------------------------------------ 接收
    def recv(self, timeout_ms: Optional[float] = None) -> bytes:
        """接收一条完整报文。超时抛 RxTimeout。"""
        timeout = (timeout_ms if timeout_ms is not None else 1000.0) / 1000.0
        deadline = time.monotonic() + timeout

        first = self._recv_frame(deadline)
        if first is None:
            raise RxTimeout("等待首帧/单帧超时")

        pci = first[0]
        frame_type = pci & 0xF0

        if frame_type == 0x00:                   # 单帧
            length = pci & 0x0F
            if length == 0 or length > 7:
                raise FrameFormatError(f"非法单帧长度 {length}")
            return first[1:1 + length]

        if frame_type == 0x10:                   # 首帧
            total = ((pci & 0x0F) << 8) | first[1]
            if total <= 7:
                raise FrameFormatError("首帧长度应大于 7")
            buf = bytearray(first[2:8])
            self._send_flow_control(bs=8, stmin=0)
            sn = 1
            received = 0
            while len(buf) < total:
                deadline = time.monotonic() + self.n_cr
                frame = self._recv_frame(deadline)
                if frame is None:
                    raise RxTimeout("等待连续帧超时（N_Cr）")
                if (frame[0] & 0xF0) != 0x20:
                    raise FrameFormatError(f"期望连续帧，收到 PCI=0x{frame[0]:02X}")
                if (frame[0] & 0x0F) != sn:
                    # 序号不匹配必须丢弃整包，不能只丢这一帧
                    raise SequenceError(
                        f"连续帧序号错：期望 {sn}，收到 {frame[0] & 0x0F}")
                buf += frame[1:8]
                sn = (sn + 1) & 0x0F
                received += 1
                if received % 8 == 0 and len(buf) < total:
                    self._send_flow_control(bs=8, stmin=0)
            return bytes(buf[:total])

        raise FrameFormatError(f"非法帧类型 PCI=0x{pci:02X}")

    def _send_flow_control(self, bs: int, stmin: int) -> None:
        self._send_frame(bytes([0x30, bs & 0xFF, stmin & 0xFF]))

    def _recv_frame(self, deadline: float) -> Optional[bytes]:
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            msg = self.bus.recv(timeout=remain)
            if msg is None:
                return None
            if msg.is_error_frame or msg.is_remote_frame:
                continue
            if msg.arbitration_id != self.rx_id:
                continue
            return bytes(msg.data)
