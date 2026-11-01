# -*- coding: utf-8 -*-
"""ISO-TP 收发测试：不需要硬件，用 python-can 的 virtual 总线。

B 端跑一个回显线程：收到完整报文后原样发回，A 端再收。
这样同时验证了发送方向（FF/CF + 等 FC）和接收方向（回 FC + 重组）。
"""
import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))

import can  # noqa: E402
from isotp import IsoTp, RxTimeout  # noqa: E402


class EchoEndpoint(threading.Thread):
    """B 端：收到的每条报文原样回发。"""

    def __init__(self, tp: IsoTp):
        super().__init__(daemon=True)
        self.tp = tp
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        while not self._stop.is_set():
            try:
                data = self.tp.recv(timeout_ms=100)
            except RxTimeout:
                continue
            except Exception:                    # noqa: BLE001 - 测试线程不因异常退出
                continue
            try:
                self.tp.send(data)
            except Exception:                    # noqa: BLE001
                pass


class IsotpPairTest(unittest.TestCase):
    def setUp(self):
        # 每个用例独立通道，避免上个用例残留的帧干扰
        channel = f"isotp-test-{time.time_ns()}"
        self.bus_a = can.Bus(interface="virtual", channel=channel)
        self.bus_b = can.Bus(interface="virtual", channel=channel)
        self.a = IsoTp(self.bus_a, tx_id=0x7E0, rx_id=0x7E8)
        self.b = IsoTp(self.bus_b, tx_id=0x7E8, rx_id=0x7E0)
        self.echo = EchoEndpoint(self.b)
        self.echo.start()
        time.sleep(0.02)

    def tearDown(self):
        self.echo.stop()
        self.bus_a.shutdown()
        self.bus_b.shutdown()

    def _roundtrip(self, payload: bytes):
        self.a.send(payload)
        return self.a.recv(timeout_ms=3000)

    def test_single_frame(self):
        payload = b"\x02\x10\x03"
        self.assertEqual(self._roundtrip(payload), payload)

    def test_seven_bytes(self):
        payload = bytes(range(7))
        self.assertEqual(self._roundtrip(payload), payload)

    def test_eight_bytes(self):
        payload = bytes(range(8))
        self.assertEqual(self._roundtrip(payload), payload)

    def test_sn_wrap(self):
        payload = bytes((i * 3) & 0xFF for i in range(200))
        self.assertEqual(self._roundtrip(payload), payload)

    def test_max_length(self):
        payload = bytes((i * 7) & 0xFF for i in range(4095))
        self.assertEqual(self._roundtrip(payload), payload)

    def test_timeout(self):
        with self.assertRaises(RxTimeout):
            self.a.recv(timeout_ms=100)


if __name__ == "__main__":
    unittest.main(verbosity=2)
