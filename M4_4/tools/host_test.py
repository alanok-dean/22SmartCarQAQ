#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
host_test.py —— M4-4 上位机参考实现 / 联调测试工具

功能：
  1. 发送 CMD_SPEED 指令帧
  2. 逐字节解析 STATUS 状态帧（与 MCU 端同一套状态机）
  3. 统计好帧/坏帧，验证"插脏字节后能自动重同步"
  4. --inject 参数可主动往串口里灌脏字节，模拟现场破坏测试

协议见 docs/protocol_v1.0.md。

用法：
  python host_test.py -p COM5                 # 正常联调 10 秒
  python host_test.py -p COM5 --inject 20     # 每收到 20 个好帧插一次脏字节
  python host_test.py -p COM5 --send 300,300  # 下发左右轮目标
"""
import argparse
import random
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("缺依赖：pip install pyserial")

SOF = b"\xAA\x55"
VER = 0x01
LEN_MAX = 64

MSG_CMD_SPEED = 0x01
MSG_STATUS = 0x02

PST_SOF1, PST_SOF2, PST_BODY, PST_CRC_LO, PST_CRC_HI = range(5)


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_frame(msg_id: int, seq: int, payload: bytes) -> bytes:
    body = bytes([VER]) + struct.pack("<H", len(payload)) + bytes([msg_id, seq]) + payload
    return SOF + body + struct.pack("<H", crc16_ccitt_false(body))


class Parser:
    def __init__(self):
        self.state = PST_SOF1
        self.body = bytearray()
        self.expect_len = -1
        self.crc_calc = 0
        self.good = 0
        self.bad = 0

    def reset(self):
        self.state = PST_SOF1
        self.body.clear()
        self.expect_len = -1

    def feed(self, b: int):
        """返回 (msg_id, seq, payload) 或 None"""
        if self.state == PST_SOF1:
            if b == 0xAA:
                self.state = PST_SOF2
            return None

        if self.state == PST_SOF2:
            if b == 0x55:
                self.state = PST_BODY
                self.body.clear()
                self.expect_len = -1
            else:
                self.bad += 1
                self.state = PST_SOF2 if b == 0xAA else PST_SOF1
            return None

        if self.state == PST_BODY:
            self.body.append(b)
            if len(self.body) == 2 and self.body[0] != VER:
                self.bad += 1
                self.reset()
                return None
            if len(self.body) == 3:
                self.expect_len = struct.unpack("<H", bytes(self.body[1:3]))[0]
                if self.expect_len > LEN_MAX:
                    self.bad += 1
                    self.reset()
                    return None
            if self.expect_len >= 0 and len(self.body) == 5 + self.expect_len:
                self.state = PST_CRC_LO
            return None

        if self.state == PST_CRC_LO:
            self.crc_calc = crc16_ccitt_false(bytes(self.body))
            if (self.crc_calc & 0xFF) != b:
                self.bad += 1
                self.reset()
                return None
            self.state = PST_CRC_HI
            return None

        if self.state == PST_CRC_HI:
            if (self.crc_calc >> 8) != b:
                self.bad += 1
                self.reset()
                return None
            msg_id = self.body[3]
            seq = self.body[4]
            payload = bytes(self.body[5:])
            self.good += 1
            self.reset()
            return (msg_id, seq, payload)

        self.reset()
        return None


def parse_status(payload: bytes):
    if len(payload) != 10:
        return None
    l_rpm, r_rpm, pitch, hb, bad = struct.unpack("<HHhHH", payload)
    return dict(l_rpm=l_rpm, r_rpm=r_rpm, pitch=pitch / 10.0, hb=hb, bad=bad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", required=True, help="串口号，如 COM5 或 /dev/ttyUSB0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--send", default="300,300", help="下发左右轮目标 r/min")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--inject", type=int, default=0, help="每 N 个好帧插一次脏字节")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    print(f"[INFO] {args.port} @ {args.baud}")

    target_l, target_r = (int(v) for v in args.send.split(","))
    payload = struct.pack("<hh", target_l, target_r)
    frame = build_frame(MSG_CMD_SPEED, 0x01, payload)
    ser.write(frame)
    print(f"[TX] CMD_SPEED L={target_l} R={target_r}  {' '.join(f'{b:02X}' for b in frame)}")

    parser = Parser()
    t0 = time.time()
    last_print = 0.0
    injected = 0

    while time.time() - t0 < args.seconds:
        chunk = ser.read(64)
        for b in chunk:
            result = parser.feed(b)
            if result is None:
                continue
            msg_id, seq, pl = result
            if msg_id == MSG_STATUS:
                info = parse_status(pl)
                if info and time.time() - last_print > 0.25:
                    print(f"[RX] seq={seq:3d} L={info['l_rpm']:5d} R={info['r_rpm']:5d} "
                          f"pitch={info['pitch']:6.1f} hb={info['hb']:5d} bad={info['bad']:3d} "
                          f"| good={parser.good} bad={parser.bad}")
                    last_print = time.time()

        if args.inject and parser.good > 0 and parser.good % args.inject == 0:
            noise = bytes(random.getrandbits(8) for _ in range(5))
            ser.write(noise)
            injected += 1
            parser.good += 1          # 避免每轮都重复注入

    print(f"\n[SUM] 好帧={parser.good} 坏帧={parser.bad} 注入脏包={injected}")
    ser.close()


if __name__ == "__main__":
    main()
