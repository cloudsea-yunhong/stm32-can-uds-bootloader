 # -*- coding: utf-8 -*-
"""CAN 设备封装：屏蔽 gs_usb / pcan / slcan 三种后端差异。

用法：
    from can_driver import open_bus
    bus = open_bus("gs_usb")            # candleLight 固件
    bus = open_bus("pcan")              # PCAN 固件（需 PCAN-Basic）
    bus = open_bus("slcan", "COM5")     # slcan 固件
    bus = open_bus("virtual")           # 无硬件，配合 tests/fake_ecu.py
"""
from __future__ import annotations

import time
from typing import Optional

import can


DEFAULT_BITRATE = 500_000

# 诊断寻址（ISO 14229 物理寻址）
UDS_REQ_ID = 0x7E0
UDS_RESP_ID = 0x7E8


class BusOpenError(RuntimeError):
    pass


def open_bus(backend: str = "gs_usb", channel: Optional[str] = None,
             bitrate: int = DEFAULT_BITRATE, **kwargs) -> can.BusABC:
    """按后端名打开总线。

    backend 取值：
        gs_usb  —— candleLight 固件（推荐）
        pcan    —— PCAN 固件，需安装 PEAK 驱动 + PCAN-Basic
        slcan   —— 虚拟串口，channel 传 "COM5" 之类
        virtual —— python-can 内置虚拟总线，用于无硬件测试
    """
    try:
        if backend == "gs_usb":
            return can.Bus(interface="gs_usb", channel=int(channel or 0),
                           bitrate=bitrate, **kwargs)
        if backend == "pcan":
            return can.Bus(interface="pcan",
                           channel=channel or "PCAN_USBBUS1",
                           bitrate=bitrate, **kwargs)
        if backend == "slcan":
            if not channel:
                raise BusOpenError("slcan 需要 --channel（例如 COM5）")
            return can.Bus(interface="slcan", channel=channel,
                           bitrate=bitrate, **kwargs)
        if backend == "virtual":
            return can.Bus(interface="virtual", channel=channel or "vcan0",
                           **kwargs)
        raise BusOpenError(f"未知后端：{backend}")
    except BusOpenError:
        raise
    except Exception as exc:                       # noqa: BLE001 - 需要原样报给用户
        raise BusOpenError(f"打开 {backend} 失败：{exc}") from exc


def list_available() -> list[dict]:
    """列出系统里 python-can 认到的接口，排查驱动问题最快的手段。"""
    return list(can.detect_available_configs())


class BusMonitor:
    """把总线上的帧按时间戳打印出来，用于手工调试。"""

    def __init__(self, bus: can.BusABC, show_data_len: bool = True):
        self.bus = bus
        self.show_data_len = show_data_len

    def run(self, duration_s: Optional[float] = None) -> None:
        deadline = None if duration_s is None else time.monotonic() + duration_s
        while deadline is None or time.monotonic() < deadline:
            msg = self.bus.recv(timeout=0.2)
            if msg is None:
                continue
            data = " ".join(f"{b:02X}" for b in msg.data)
            if self.show_data_len:
                print(f"{msg.timestamp:10.3f}  {msg.arbitration_id:03X}  "
                      f"[{msg.dlc}]  {data}")
            else:
                print(f"{msg.timestamp:10.3f}  {msg.arbitration_id:03X}  {data}")
