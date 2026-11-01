# -*- coding: utf-8 -*-
"""CRC32 测试：两个实现互相验证，并确认与 zlib 变体不同。"""
import os
import random
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))

from crc_hw import crc32_stm32, crc32_stm32_fast, pad_to_word  # noqa: E402


class CrcTest(unittest.TestCase):
    def test_two_implementations_agree(self):
        rnd = random.Random(20261027)
        for _ in range(20):
            data = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(4, 256)))
            self.assertEqual(crc32_stm32(data), crc32_stm32_fast(data))

    def test_differs_from_zlib(self):
        data = b"123456789"
        self.assertNotEqual(crc32_stm32(data), zlib.crc32(data))

    def test_padding(self):
        self.assertEqual(len(pad_to_word(b"\x01\x02\x03")), 4)
        self.assertEqual(pad_to_word(b"\x01\x02\x03"), b"\x01\x02\x03\xFF")
        self.assertEqual(len(pad_to_word(b"\x01\x02\x03\x04")), 4)

    def test_empty(self):
        self.assertEqual(crc32_stm32(b""), 0xFFFFFFFF)


if __name__ == "__main__":
    unittest.main(verbosity=2)
