# -*- coding: utf-8 -*-
"""发一帧 UDS 10 03，看节点是否回应 0x7E8。"""
import can
import logging

logging.getLogger("can").setLevel(logging.ERROR)

def main():
    try:
        bus = can.Bus(interface="pcan", channel="PCAN_USBBUS1", bitrate=500000)
    except Exception as e:
        print("打开 PCAN 失败：", e)
        return 1
    msg = can.Message(arbitration_id=0x7E0, data=[0x02, 0x10, 0x03], is_extended_id=False)
    bus.send(msg)
    print("已发送 0x7E0: 02 10 03")
    try:
        print("适配器状态：", bus.state)
    except Exception as e:
        print("读状态失败：", e)
    resp = bus.recv(timeout=1.0)
    if resp is None:
        print("1 秒内没有收到响应（0x7E8）")
        bus.shutdown()
        return 2
    print(f"收到 0x{resp.arbitration_id:03X}: {resp.data.hex(' ')}")
    bus.shutdown()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())