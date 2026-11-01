# -*- coding: utf-8 -*-
"""端到端测试：假 ECU + 真上位机，完整跑一遍 UDS 刷写。"""
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))

import can  # noqa: E402
from fake_ecu import FakeEcu  # noqa: E402
from flasher import Flasher  # noqa: E402
from isotp import IsoTp  # noqa: E402
from uds import UdsClient, DID_VERSION  # noqa: E402


class FlashEndToEndTest(unittest.TestCase):
    def setUp(self):
        self.channel = f"flash-test-{time.time_ns()}"
        self.bus_pc = can.Bus(interface="virtual", channel=self.channel)
        self.bus_ecu = can.Bus(interface="virtual", channel=self.channel)
        self.ecu = FakeEcu(self.bus_ecu, verbose=False)
        self.ecu.start()
        self.tp = IsoTp(self.bus_pc, tx_id=0x7E0, rx_id=0x7E8)
        self.uds = UdsClient(self.tp)
        time.sleep(0.05)

    def tearDown(self):
        self.ecu.stop()
        self.bus_pc.shutdown()
        self.bus_ecu.shutdown()

    def test_read_version(self):
        self.assertEqual(self.uds.read_did(DID_VERSION), b"\x01\x02\x03\x00")

    def test_session_and_unlock(self):
        self.uds.session_control(0x03)
        self.uds.unlock()
        self.uds.programming_session()
        self.assertEqual(self.ecu.session, 0x02)

    def test_switch_area(self):
        """切区：进会话 → 切到 A → 读回当前区应为 0。"""
        self.uds.enter_programming()
        self.uds.switch_area(1)
        self.assertEqual(self.uds.read_active_area(), 1)
        self.uds.switch_area(0)
        self.assertEqual(self.uds.read_active_area(), 0)

    def test_flash_small_firmware(self):
        firmware = bytes((i * 5 + 7) & 0xFF for i in range(2000))
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as fp:
            fp.write(firmware)
            path = fp.name
        try:
            flasher = Flasher(self.uds, block_length=66, verbose=False)
            result = flasher.flash(path, 0x08040000)
            self.assertTrue(result.ok)
            self.assertGreater(result.blocks, 1)
            # 校验写进去的内容
            self.assertEqual(bytes(self.ecu.region[:len(firmware)]), firmware)
        finally:
            os.unlink(path)

    def test_flash_rejects_bad_crc(self):
        firmware = b"\xAA" * 64
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as fp:
            fp.write(firmware)
            path = fp.name
        try:
            flasher = Flasher(self.uds, block_length=66, verbose=False)
            # 手动走一遍，最后用错误 CRC 触发负响应
            self.uds.enter_programming()
            self.uds.erase_region(0x08040000, len(firmware))
            self.uds.request_download(0x08040000, len(firmware))
            self.uds.transfer_data(1, firmware)
            self.uds.request_transfer_exit()
            from uds import NrcError
            with self.assertRaises(NrcError):
                self.uds.check_crc(0x08040000, len(firmware), 0xDEADBEEF)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
