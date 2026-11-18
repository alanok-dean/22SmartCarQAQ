#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serial_mon.py —— 串口文本监视器（看 MCU 的 printf 输出）

用途：比 VOFA+ 轻，直接看 [HB]/[CMD]/[KEY] 这类文本日志。
      VOFA+ 的 FireWater 引擎只认纯数值 CSV，看文本要用 RawData 控件，
      临时排查用这个更快。

用法：
  python serial_mon.py -p COM8
  python serial_mon.py -p COM8 -b 115200 --seconds 10
  python serial_mon.py -p COM8 --save ../data/board_log.txt
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("缺依赖：pip install pyserial")

VER = 0x01


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_cmd(l_rpm: int, r_rpm: int) -> bytes:
    payload = struct.pack("<hh", l_rpm, r_rpm)
    body = bytes([VER]) + struct.pack("<H", len(payload)) + bytes([0x01, 0x01]) + payload
    return b"\xAA\x55" + body + struct.pack("<H", crc16_ccitt_false(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", required=True)
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=0, help="0 = 一直跑到 Ctrl+C")
    ap.add_argument("--save", default=None, help="同时保存到文件")
    ap.add_argument("--send", default=None, help="连上后发一条 CMD_SPEED，格式 L,R（如 300,300）")
    ap.add_argument("--only-text", action="store_true",
                    help="只显示含 '[' 的行（过滤掉二进制状态帧的乱码）")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"[MON] {args.port} @ {args.baud}  (Ctrl+C 退出)")

    if args.send:
        l, r = (int(v) for v in args.send.split(","))
        frame = build_cmd(l, r)
        ser.write(frame)
        print(f"[MON] 已发送 CMD_SPEED L={l} R={r}:",
              " ".join(f"{b:02X}" for b in frame))

    fh = open(args.save, "w", encoding="utf-8") if args.save else None
    t0 = time.time()
    try:
        while True:
            if args.seconds and time.time() - t0 > args.seconds:
                break
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "ignore").strip()
            if not line:
                continue
            if args.only_text and "[" not in line:
                continue
            print(line)
            if fh:
                fh.write(line + "\n")
                fh.flush()
    except KeyboardInterrupt:
        print("\n[MON] 已停止")
    finally:
        ser.close()
        if fh:
            fh.close()
            print(f"[MON] 已保存到 {args.save}")


if __name__ == "__main__":
    main()
