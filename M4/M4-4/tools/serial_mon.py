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
import re
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


def ascii_only(text: str) -> str:
    """丢掉所有非可打印 ASCII 字符。

    二进制 STATUS 帧被 UTF-8 解码后会变成各种奇怪 Unicode 字符，
    直接 print 到 GBK 控制台会抛 UnicodeEncodeError 把脚本打死。
    在这里先清掉，屏幕和文件都只剩干净文本。
    """
    return "".join(ch for ch in text if 32 <= ord(ch) < 127)


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
    ap.add_argument("--revs", type=float, default=0,
                    help="退出时按这个圈数算 CPR，如 --revs 10")
    args = ap.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"[MON] {args.port} @ {args.baud}  (Ctrl+C 退出)")

    if args.send:
        l, r = (int(v) for v in args.send.split(","))
        frame = build_cmd(l, r)
        ser.write(frame)
        print(f"[MON] 已发送 CMD_SPEED L={l} R={r}:",
              " ".join(f"{b:02X}" for b in frame))

    fh = open(args.save, "w", encoding="utf-8") if args.save else None
    enc = []          # 记录所有 [ENC] 行的 total 值，退出时给首末值
    t0 = time.time()
    try:
        while True:
            if args.seconds and time.time() - t0 > args.seconds:
                break
            raw = ser.readline()
            if not raw:
                continue
            line = ascii_only(raw.decode("utf-8", "ignore")).strip()
            if not line:
                continue
            if args.only_text and "[" not in line:
                continue
            print(line)
            m = re.search(r"\[ENC\].*?total=(-?\d+)", line)
            if m:
                enc.append(int(m.group(1)))
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

    if len(enc) >= 2:
        start, end = enc[0], enc[-1]
        delta = end - start
        print(f"[ENC] 首行 total={start}  末行 total={end}  Delta={delta}")
        if args.revs:
            print(f"[ENC] CPR = {delta} / {args.revs:g} = {delta / args.revs:.1f}")
    elif enc:
        print(f"[ENC] 只收到 {len(enc)} 行，无法比较首末值")


if __name__ == "__main__":
    main()
