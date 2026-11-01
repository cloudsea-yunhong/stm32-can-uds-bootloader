# -*- coding: utf-8 -*-
"""命令行入口：probe / monitor / flash / read / raw。

示例：
    python cli.py probe
    python cli.py monitor --backend gs_usb
    python cli.py flash --bin firmware/app_b.bin --addr 0x08040000 --backend gs_usb
    python cli.py flash --bin firmware/app_b.bin --addr 0x08040000 --fake   # 无硬件自测
"""
from __future__ import annotations

import argparse
import sys
import time

from can_driver import list_available, open_bus, BusMonitor, UDS_REQ_ID, UDS_RESP_ID
from flasher import DEFAULT_BLOCK_LENGTH, Flasher
from isotp import IsoTp
from uds import UdsClient, NrcError, UdsError, DID_VERSION


def parse_int(text: str) -> int:
    return int(text, 0)


def build_client(args) -> tuple[IsoTp, UdsClient, object]:
    bus = open_bus(args.backend, args.channel, args.bitrate)
    tp = IsoTp(bus, tx_id=UDS_REQ_ID, rx_id=UDS_RESP_ID,
               verbose=args.verbose)
    uds = UdsClient(tp, verbose=args.verbose)
    return tp, uds, bus


def cmd_probe(args) -> int:
    configs = list_available()
    if not configs:
        print("没有检测到任何 python-can 接口。")
        print("检查：适配器是否插好、驱动是否安装、后端名是否正确。")
        return 1
    for cfg in configs:
        print(cfg)
    return 0


def cmd_monitor(args) -> int:
    bus = open_bus(args.backend, args.channel, args.bitrate)
    print(f"监听中（{args.duration}s，Ctrl+C 结束）……")
    try:
        BusMonitor(bus).run(duration_s=args.duration)
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()
    return 0


def cmd_read(args) -> int:
    tp, uds, bus = build_client(args)
    try:
        data = uds.read_did(parse_int(args.did))
        print(f"DID 0x{parse_int(args.did):04X} -> {data.hex(' ')}")
        if parse_int(args.did) == DID_VERSION:
            print("版本:", ".".join(str(b) for b in data))
    finally:
        bus.shutdown()
    return 0


def cmd_raw(args) -> int:
    payload = bytes.fromhex(args.hex.replace(",", " ").replace("0x", ""))
    tp, uds, bus = build_client(args)
    try:
        resp = uds.request(payload)
        print("响应:", resp.hex(" "))
    except NrcError as exc:
        print("负响应:", exc)
        return 2
    finally:
        bus.shutdown()
    return 0


def cmd_rollback(args) -> int:
    """把启动区切回 A 或 B，并复位节点（不写固件）。"""
    area = 0 if args.to.lower() == "a" else 1
    fake_ecu = None
    bus = open_bus(args.backend, args.channel, args.bitrate)
    if args.fake:
        from fake_ecu import FakeEcu
        bus_ecu = open_bus("virtual", args.channel or "vcan0")
        fake_ecu = FakeEcu(bus_ecu, verbose=args.verbose)
        fake_ecu.start()
        time.sleep(0.1)
    tp = IsoTp(bus, tx_id=UDS_REQ_ID, rx_id=UDS_RESP_ID, verbose=False)
    uds = UdsClient(tp, verbose=args.verbose)
    try:
        uds.enter_programming()
        # App 收到 10 02 会复位交权；Bootloader 的会话/解锁是全新的，需要再握手一次
        time.sleep(0.8)
        uds.enter_programming()
        uds.switch_area(area)
        print(f"已切换启动区到 App-{'A' if area == 0 else 'B'}，正在复位……")
        uds.ecu_reset(0x01)
        print("复位指令已发送。注意：若目标区固件无效，节点会停在 Bootloader。")
    finally:
        if fake_ecu:
            fake_ecu.stop()
        bus.shutdown()
    return 0


def cmd_flash(args) -> int:
    fake_ecu = None
    bus = open_bus(args.backend, args.channel, args.bitrate)
    if args.fake:
        from fake_ecu import FakeEcu
        bus_ecu = open_bus("virtual", args.channel or "vcan0")
        fake_ecu = FakeEcu(bus_ecu, verbose=args.verbose)
        fake_ecu.start()
        time.sleep(0.1)

    tp = IsoTp(bus, tx_id=UDS_REQ_ID, rx_id=UDS_RESP_ID, verbose=False)
    uds = UdsClient(tp, verbose=args.verbose)
    flasher = Flasher(uds, block_length=args.block, verbose=True)

    rc = 0
    try:
        before = uds.read_did(DID_VERSION)
        print("升级前版本:", ".".join(str(b) for b in before))
        result = flasher.flash(args.bin, parse_int(args.addr),
                               resume=args.resume, uid_fold=parse_int(args.uid)
                               if args.uid else 0)
        print(f"结果: {result.message}  "
              f"{result.firmware_size} 字节 / {result.blocks} 块 / "
              f"{result.elapsed_s:.2f} s")
        if args.area is not None:
            flasher.switch_and_reset(args.area)
            print(f"已切到 App-{'A' if args.area == 0 else 'B'} 并复位")
    except NrcError as exc:
        print("刷写失败（负响应）:", exc)
        rc = 2
    except UdsError as exc:
        print("刷写失败（协议层）:", exc)
        rc = 3
    except Exception as exc:                              # noqa: BLE001
        print("刷写失败:", exc)
        rc = 4
    finally:
        if fake_ecu:
            fake_ecu.stop()
        bus.shutdown()
    return rc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="CAN Bootloader / UDS 刷写工具")
    parser.add_argument("--backend", default="gs_usb",
                        choices=["gs_usb", "pcan", "slcan", "virtual"],
                        help="python-can 后端")
    parser.add_argument("--channel", default=None, help="通道（slcan 传 COM5）")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("-v", "--verbose", action="store_true")

    sub = parser.add_subparsers(dest="cmd", required=True)

    def add_verbose(p):
        # 允许把 -v 写在子命令后面，且不覆盖全局的 -v
        p.add_argument("-v", "--verbose", action="store_true",
                       default=argparse.SUPPRESS)

    p_probe = sub.add_parser("probe", help="列出可用接口")
    add_verbose(p_probe)
    p_probe.set_defaults(func=cmd_probe)

    p_mon = sub.add_parser("monitor", help="监听总线")
    add_verbose(p_mon)
    p_mon.add_argument("--duration", type=float, default=10.0)
    p_mon.set_defaults(func=cmd_monitor)

    p_read = sub.add_parser("read", help="读 DID")
    add_verbose(p_read)
    p_read.add_argument("--did", required=True, help="如 0xF195")
    p_read.set_defaults(func=cmd_read)

    p_raw = sub.add_parser("raw", help="发送一条 UDS 请求")
    add_verbose(p_raw)
    p_raw.add_argument("--hex", required=True, help='如 "10 03"')
    p_raw.set_defaults(func=cmd_raw)

    p_rb = sub.add_parser("rollback", help="切回 A/B 区并复位（不写固件）")
    add_verbose(p_rb)
    p_rb.add_argument("--to", required=True, choices=["a", "b", "A", "B"],
                      help="切回哪个区")
    p_rb.add_argument("--fake", action="store_true",
                      help="启动内置假 ECU（虚拟总线）")
    p_rb.set_defaults(func=cmd_rollback)

    p_flash = sub.add_parser("flash", help="刷写固件")
    add_verbose(p_flash)
    p_flash.add_argument("--bin", required=True)
    p_flash.add_argument("--addr", required=True, help="目标区地址，如 0x08040000")
    p_flash.add_argument("--block", type=int, default=DEFAULT_BLOCK_LENGTH,
                         help="0x36 块长度（含 SID+BSC），默认 642")
    p_flash.add_argument("--resume", action="store_true", help="断点续传")
    p_flash.add_argument("--area", type=int, choices=[0, 1], default=None,
                         help="刷完后切区：0=App-A，1=App-B")
    p_flash.add_argument("--uid", default=None, help="UID 折叠值（十六进制）")
    p_flash.add_argument("--fake", action="store_true",
                         help="启动内置假 ECU（虚拟总线）")
    p_flash.set_defaults(func=cmd_flash)

    args = parser.parse_args(argv)
    if args.fake and args.backend == "gs_usb":
        args.backend = "virtual"
    try:
        return args.func(args)
    except KeyboardInterrupt:
        print("\n已中断")
        return 130


if __name__ == "__main__":
    sys.exit(main())
