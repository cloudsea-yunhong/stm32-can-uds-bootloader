# -*- coding: utf-8 -*-
"""测试 PCAN 适配器是否能被 python-can 打开。"""
import can
import logging

logging.getLogger("can").setLevel(logging.ERROR)

CHANNELS = ["PCAN_USBBUS1", "PCAN_USBBUS2", "PCAN_USBBUS3", "PCAN_USBBUS4"]

def main():
    for ch in CHANNELS:
        try:
            bus = can.Bus(interface="pcan", channel=ch, bitrate=500000)
        except Exception as e:
            print(f"[FAIL] {ch}: {e}")
            continue
        print(f"[ OK ] {ch} 已打开，500 kbps")
        bus.shutdown()
        return 0
    print("\n没有可用的 PCAN 通道。检查：")
    print("1) 适配器是否插好，设备管理器是否显示 PCAN-USB")
    print("2) PEAK 驱动是否安装")
    print("3) 是否被其他软件占用（pcanview / 上位机）")
    return 1

if __name__ == "__main__":
    raise SystemExit(main())