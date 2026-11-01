# -*- coding: utf-8 -*-
"""UDS（ISO 14229）客户端：9 项服务 + 负响应 + 0x78 挂起处理。

服务清单与项目设计说明书 6.1 节一致：
    0x10 会话控制   0x11 ECU 复位   0x22 读数据    0x27 安全访问
    0x31 例程控制   0x34 请求下载   0x36 传输数据  0x37 结束传输
    0x3E 在线保持
"""
from __future__ import annotations

import time
from typing import Callable, Optional

from isotp import IsoTp, IsoTpError, RxTimeout


# ---------------------------------------------------------------- 负响应码
NRC_NAMES = {
    0x10: "generalReject",
    0x11: "serviceNotSupported",
    0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat",
    0x22: "conditionsNotCorrect",
    0x24: "requestSequenceError",
    0x31: "requestOutOfRange",
    0x33: "securityAccessDenied",
    0x35: "invalidKey",
    0x36: "exceedNumberOfAttempts",
    0x37: "requiredTimeDelayNotExpired",
    0x72: "generalProgrammingFailure",
    0x78: "requestCorrectlyReceivedResponsePending",
    0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}


class UdsError(Exception):
    pass


class NrcError(UdsError):
    """ECU 返回 7F <SID> <NRC>。"""

    def __init__(self, sid: int, nrc: int):
        self.sid = sid
        self.nrc = nrc
        name = NRC_NAMES.get(nrc, "unknown")
        super().__init__(f"0x{sid:02X} 被拒绝：NRC 0x{nrc:02X} {name}")


class ResponseTimeout(UdsError):
    pass


class UnexpectedResponse(UdsError):
    pass


# ---------------------------------------------------------------- 安全访问
def teaching_key(seed: int, uid_fold: int = 0) -> int:
    """与板端一致的教学版密钥算法（见设计说明书 6.3 节）。

    真实项目应使用 AES-CMAC + HSM；这里只是为了把“挑战-应答 + 设备绑定”
    这条链路走通。
    """
    k = (seed ^ uid_fold) & 0xFFFFFFFF
    k ^= 0xA5A5A5A5
    k = ((k << 3) | (k >> 29)) & 0xFFFFFFFF
    k = (~k) & 0xFFFFFFFF
    return (k ^ 0x5A5A5A5A) & 0xFFFFFFFF


# ---------------------------------------------------------------- DID 表
DID_SESSION = 0xF186
DID_VIN = 0xF190
DID_VERSION = 0xF195
DID_PART_NUMBER = 0xF187
DID_ACTIVE_AREA = 0xFF00        # 自定义：当前运行区 0=A，1=B
DID_LAST_BLOCK = 0xFF01         # 自定义：节点侧最后写入块号
DID_UID_FOLD = 0xFF02           # 自定义：96 位 UID 折叠值（用于密钥）


class UdsClient:
    """基于 IsoTp 的 UDS 客户端。"""

    def __init__(self, tp: IsoTp, p2_ms: float = 50.0,
                 p2_star_ms: float = 5000.0, verbose: bool = False) -> None:
        self.tp = tp
        self.p2 = p2_ms / 1000.0
        self.p2_star = p2_star_ms / 1000.0
        self.verbose = verbose
        self._unlocked_level = 0

    # ------------------------------------------------------------ 基础收发
    def request(self, payload: bytes, timeout_ms: Optional[float] = None) -> bytes:
        self.tp.send(payload)
        deadline = time.monotonic() + (timeout_ms / 1000.0 if timeout_ms else self.p2_star)
        while time.monotonic() < deadline:
            remain_ms = max(1.0, (deadline - time.monotonic()) * 1000.0)
            try:
                resp = self.tp.recv(timeout_ms=remain_ms)
            except RxTimeout as exc:
                raise ResponseTimeout(f"等待响应超时：0x{payload[0]:02X}") from exc
            if not resp:
                continue
            if resp[0] == 0x7F:
                sid, nrc = resp[1], resp[2]
                if nrc == 0x78:                      # responsePending
                    deadline = time.monotonic() + self.p2_star
                    if self.verbose:
                        print(f"  [uds] 0x{sid:02X} 收到 0x78，继续等待")
                    continue
                raise NrcError(sid, nrc)
            if resp[0] != (payload[0] + 0x40):
                raise UnexpectedResponse(
                    f"响应 SID 0x{resp[0]:02X} 与请求 0x{payload[0]:02X} 不匹配")
            return resp
        raise ResponseTimeout(f"等待最终响应超时：0x{payload[0]:02X}")

    # ------------------------------------------------------------ 0x10
    def session_control(self, session: int = 0x03) -> bytes:
        resp = self.request(bytes([0x10, session]))
        if self.verbose:
            print(f"  [uds] 会话 0x{session:02X} 已进入")
        return resp

    def default_session(self) -> bytes:
        return self.session_control(0x01)

    def programming_session(self) -> bytes:
        return self.session_control(0x02)

    # ------------------------------------------------------------ 0x11
    def ecu_reset(self, reset_type: int = 0x01) -> bytes:
        return self.request(bytes([0x11, reset_type]))

    # ------------------------------------------------------------ 0x22
    def read_did(self, did: int) -> bytes:
        resp = self.request(bytes([0x22, (did >> 8) & 0xFF, did & 0xFF]))
        # 62 <DID_H> <DID_L> <data...>
        return resp[3:]

    def read_version(self) -> str:
        raw = self.read_did(DID_VERSION)
        return ".".join(str(b) for b in raw)

    def read_active_area(self) -> int:
        raw = self.read_did(DID_ACTIVE_AREA)
        return raw[0] if raw else -1

    def read_last_block(self) -> int:
        raw = self.read_did(DID_LAST_BLOCK)
        return int.from_bytes(raw, "big") if raw else 0

    # ------------------------------------------------------------ 0x27
    def request_seed(self, level: int = 0x01) -> int:
        resp = self.request(bytes([0x27, level]))
        seed_bytes = resp[2:]
        if len(seed_bytes) > 4:
            raise UnexpectedResponse("种子长度超过 4 字节")
        return int.from_bytes(seed_bytes, "big")

    def send_key(self, key: int, level: int = 0x02) -> None:
        self.request(bytes([0x27, level]) + key.to_bytes(4, "big"))
        self._unlocked_level = level

    def unlock(self, key_fn: Callable[[int, int], int] = teaching_key,
               uid_fold: int = 0, level: int = 0x01,
               auto_uid: bool = True) -> None:
        """请求种子 → 计算密钥 → 发送。"""
        if auto_uid and uid_fold == 0:
            try:
                raw = self.read_did(DID_UID_FOLD)
                uid_fold = int.from_bytes(raw, "big") if raw else 0
            except (NrcError, ResponseTimeout):
                uid_fold = 0
        seed = self.request_seed(level)
        key = key_fn(seed, uid_fold)
        if self.verbose:
            print(f"  [uds] seed=0x{seed:08X} uid=0x{uid_fold:08X} key=0x{key:08X}")
        self.send_key(key, level + 1)

    # ------------------------------------------------------------ 0x31
    def routine_control(self, routine_id: int, sub: int = 0x01,
                        data: bytes = b"") -> bytes:
        payload = bytes([0x31, sub, (routine_id >> 8) & 0xFF,
                         routine_id & 0xFF]) + data
        return self.request(payload)

    def erase_region(self, addr: int, size: int) -> bytes:
        return self.routine_control(0xFF00, 0x01, addr.to_bytes(4, "big")
                                    + size.to_bytes(4, "big"))

    def check_crc(self, addr: int, size: int, expected_crc: Optional[int] = None) -> bytes:
        data = addr.to_bytes(4, "big") + size.to_bytes(4, "big")
        if expected_crc is not None:
            data += expected_crc.to_bytes(4, "big")
        return self.routine_control(0xFF01, 0x01, data)

    def switch_area(self, area: int) -> bytes:
        return self.routine_control(0xFF02, 0x01, bytes([area & 0x01]))

    # ------------------------------------------------------------ 0x34 / 0x36 / 0x37
    def request_download(self, addr: int, size: int,
                         data_format: int = 0x00,
                         alfid: int = 0x44) -> int:
        """返回 maxNumberOfBlockLength（含 SID 与块序号）。"""
        payload = (bytes([0x34, data_format, alfid])
                   + addr.to_bytes(4, "big") + size.to_bytes(4, "big"))
        resp = self.request(payload)
        # 74 <lengthFormatIdentifier> <maxNumberOfBlockLength...>
        if len(resp) < 4:
            raise UnexpectedResponse("0x34 响应过短")
        length = resp[1] & 0x0F
        raw = resp[2:2 + (length if length else 2)]
        return int.from_bytes(raw, "big")

    def transfer_data(self, block_seq: int, data: bytes) -> int:
        payload = bytes([0x36, block_seq & 0xFF]) + data
        resp = self.request(payload)
        return resp[1] if len(resp) > 1 else -1

    def request_transfer_exit(self) -> bytes:
        return self.request(bytes([0x37]))

    # ------------------------------------------------------------ 0x3E
    def tester_present(self) -> bytes:
        return self.request(bytes([0x3E, 0x00]))

    # ------------------------------------------------------------ 便捷组合
    def enter_programming(self, uid_fold: int = 0) -> None:
        """扩展会话 → 安全访问 → 编程会话。"""
        self.session_control(0x03)
        self.unlock(uid_fold=uid_fold)
        self.programming_session()
