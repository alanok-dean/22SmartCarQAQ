#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
frame_gen.py —— 上下位机协议帧生成与 CRC 复算工具

用途：
  1. 生成 M4-4 协议文档里引用的示例帧（考官会拿计算器/脚本手算核对）
  2. 独立复算任意一帧的 CRC，验证两端实现是否一致

协议摘要（详见 docs/protocol_v1.0.md）：
  SOF(2) | VER(1) | LEN(2,LE) | MSG(1) | SEQ(1) | PAYLOAD(LEN) | CRC16(2,LE)
  CRC16-CCITT-FALSE: poly=0x1021 init=0xFFFF refin=false refout=false xorout=0x0000
  CRC 覆盖范围：VER 起，到 PAYLOAD 最后一个字节（不含 SOF，不含 CRC 本身）

用法：
  python frame_gen.py --example 0          # 生成示例帧 0（速度指令）
  python frame_gen.py --example 1          # 生成示例帧 1（状态回报）
  python frame_gen.py --check AA5501...    # 复算给定帧的 CRC 并解析
"""
import argparse
import struct

SOF = b"\xAA\x55"
VER = 0x01
LEN_MAX = 64

MSG_CMD_SPEED = 0x01
MSG_STATUS = 0x02


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE：init 0xFFFF，poly 0x1021，不反转，无 xorout。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(msg_id: int, seq: int, payload: bytes) -> bytes:
    if len(payload) > LEN_MAX:
        raise ValueError(f"payload too long: {len(payload)} > {LEN_MAX}")
    body = bytes([VER]) + struct.pack("<H", len(payload)) + bytes([msg_id, seq]) + payload
    crc = crc16_ccitt_false(body)
    return SOF + body + struct.pack("<H", crc)


def parse_frame(frame: bytes) -> dict:
    if frame[:2] != SOF:
        raise ValueError("bad SOF")
    ver = frame[2]
    length = struct.unpack("<H", frame[3:5])[0]
    msg_id = frame[5]
    seq = frame[6]
    payload = frame[7:7 + length]
    crc_rx = struct.unpack("<H", frame[7 + length:9 + length])[0]
    body = frame[2:7 + length]
    return {
        "ver": ver,
        "len": length,
        "msg_id": msg_id,
        "seq": seq,
        "payload": payload,
        "crc_rx": crc_rx,
        "crc_calc": crc16_ccitt_false(body),
    }


def example_cmd_speed() -> bytes:
    # int16 target_left_rpm, int16 target_right_rpm（单位 r/min）
    payload = struct.pack("<hh", 300, 300)
    return build_frame(MSG_CMD_SPEED, seq=0x01, payload=payload)


def example_status() -> bytes:
    # uint16 left_rpm, uint16 right_rpm, int16 pitch_x10, uint16 hb, uint16 bad
    payload = struct.pack("<HHhHH", 298, 301, 123, 42, 0)
    return build_frame(MSG_STATUS, seq=0x11, payload=payload)


def dump(name: str, frame: bytes) -> None:
    print(f"--- {name} ---")
    print("HEX   : " + " ".join(f"{b:02X}" for b in frame))
    try:
        info = parse_frame(frame)
        print(f"VER={info['ver']} LEN={info['len']} MSG=0x{info['msg_id']:02X} "
              f"SEQ={info['seq']} PAYLOAD={info['payload'].hex(' ').upper()}")
        ok = "OK" if info["crc_rx"] == info["crc_calc"] else "MISMATCH"
        print(f"CRC   : rx=0x{info['crc_rx']:04X} calc=0x{info['crc_calc']:04X}  [{ok}]")
    except ValueError as exc:
        print(f"parse failed: {exc}")
    print()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--example", type=int, choices=[0, 1], help="输出文档示例帧")
    ap.add_argument("--check", type=str, help="复算给定帧的 CRC，HEX 字符串")
    args = ap.parse_args()

    if args.example == 0:
        dump("示例帧 0: CMD_SPEED 300/300 r/min", example_cmd_speed())
    elif args.example == 1:
        dump("示例帧 1: STATUS", example_status())
    elif args.check:
        raw = bytes.fromhex(args.check.replace(" ", "").replace(",", ""))
        dump("复算", raw)
    else:
        dump("示例帧 0: CMD_SPEED 300/300 r/min", example_cmd_speed())
        dump("示例帧 1: STATUS", example_status())


if __name__ == "__main__":
    main()
