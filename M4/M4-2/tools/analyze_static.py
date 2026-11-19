#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_static.py —— 分析 60 秒静置日志（M4-2）

输入：serial_mon.py 保存的原始日志（文本 + 二进制帧混排，不干净）
输出：终端统计表 + 可选曲线图

日志里的结构化行有：
  [CAL] gyro bias = 1.20 dps (50 samples), init pitch = -1.9 deg
  CSV,<t_ms>,<gyro_int>,<fused>,<acc>,<gy_dps>

本脚本做三件事：
  1. 从混排日志里可靠地抠出 CSV 行（二进制字节自动跳过）
  2. 统计融合角漂移、纯积分漂移
  3. 用 gy 列重算「不做零偏补偿」的积分，给出补偿前后对比

用法：
  python analyze_static.py ../data/static_60s_raw.txt
  python analyze_static.py ../data/static_60s_raw.txt --plot ../docs/plots/drift.png
"""
import argparse
import re
import sys

CSV_RE = re.compile(rb"CSV,(\d+),(-?\d+\.?\d*),(-?\d+\.?\d*),(-?\d+\.?\d*),(-?\d+\.?\d*)")
CAL_RE = re.compile(rb"\[CAL\] gyro bias = (-?\d+\.?\d*) dps")


def parse_log(path):
    """从可能混有二进制字节的日志中提取结构化数据"""
    with open(path, "rb") as fh:
        raw = fh.read()

    bias = None
    m = CAL_RE.search(raw)
    if m:
        bias = float(m.group(1))

    rows = []
    for mm in CSV_RE.finditer(raw):
        t_ms = int(mm.group(1))
        gyro_int = float(mm.group(2))
        fused = float(mm.group(3))
        acc = float(mm.group(4))
        gy = float(mm.group(5))
        rows.append((t_ms, gyro_int, fused, acc, gy))
    return bias, rows


def integrate(rows, use_bias, bias, start_angle):
    """按时间戳积分 gy，返回与 rows 等长的角度序列"""
    out = []
    angle = start_angle
    prev_t = rows[0][0] if rows else 0
    for (t_ms, gyro_int, fused, acc, gy) in rows:
        dt = (t_ms - prev_t) / 1000.0
        if dt <= 0 or dt > 0.5:
            dt = 0.0
        prev_t = t_ms
        rate = gy - (bias if use_bias else 0.0)
        angle += rate * dt
        out.append(angle)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", help="serial_mon.py 保存的日志路径")
    ap.add_argument("--plot", default=None, help="把曲线保存为 PNG")
    ap.add_argument("--seconds", type=float, default=60.0, help="统计窗口（秒），默认 60")
    args = ap.parse_args()

    bias, rows = parse_log(args.log)
    if len(rows) < 10:
        sys.exit("没有解析到 CSV 行，检查日志是否为本次实验保存的")

    t0 = rows[0][0]
    win = [r for r in rows if (r[0] - t0) <= args.seconds * 1000.0]
    if len(win) < 10:
        win = rows

    print(f"文件            : {args.log}")
    print(f"样本数          : {len(rows)}（窗口内 {len(win)}）")
    print(f"窗口时长        : {(win[-1][0] - win[0][0]) / 1000.0:.1f} s")
    print(f"采样率(实测)    : {len(win) / max(1e-6, (win[-1][0] - win[0][0]) / 1000.0):.1f} Hz")
    print(f"零偏(固件估计)  : {bias if bias is not None else '未找到 [CAL] 行'} dps")
    print()

    fused0, fused1 = win[0][2], win[-1][2]
    gint0, gint1 = win[0][1], win[-1][1]
    drift_fused = fused1 - fused0
    drift_gint = gint1 - gint0

    acc_series = [r[3] for r in win]
    no_bias = integrate(win, use_bias=False, bias=0.0, start_angle=win[0][3])
    drift_nobias = no_bias[-1] - no_bias[0]

    print("=" * 58)
    print("指标                        初值      末值      漂移")
    print("-" * 58)
    print(f"融合角 fused             {fused0:8.2f}  {fused1:8.2f}  {drift_fused:+8.2f} deg")
    print(f"纯积分(已补零偏) gyro_int {gint0:8.2f}  {gint1:8.2f}  {drift_gint:+8.2f} deg")
    print(f"纯积分(未补零偏)          {no_bias[0]:8.2f}  {no_bias[-1]:8.2f}  {drift_nobias:+8.2f} deg")
    print("=" * 58)
    print(f"加速度计角度 均值 {sum(acc_series)/len(acc_series):+.2f} deg, "
          f"标准差 {(__import__('statistics').pstdev(acc_series)):.2f} deg（噪声）")
    print()

    if args.plot:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            sys.exit("缺依赖：pip install matplotlib")
        import os
        os.makedirs(os.path.dirname(args.plot) or ".", exist_ok=True)

        ts = [(r[0] - t0) / 1000.0 for r in win]
        plt.figure(figsize=(9, 5))
        plt.plot(ts, [r[3] for r in win], color="0.7", linewidth=0.8, label="accel pitch (noisy)")
        plt.plot(ts, no_bias, color="tab:red", linewidth=1.4, label="gyro integrate (no bias comp)")
        plt.plot(ts, [r[1] for r in win], color="tab:orange", linewidth=1.2, label="gyro integrate (bias comp)")
        plt.plot(ts, [r[2] for r in win], color="tab:blue", linewidth=2.0, label="complementary fused")
        plt.xlabel("t (s)")
        plt.ylabel("pitch (deg)")
        plt.title("M4-2 static 60s: drift comparison")
        plt.grid(alpha=0.3)
        plt.legend(fontsize=8)
        plt.tight_layout()
        plt.savefig(args.plot, dpi=150)
        print(f"曲线已保存: {args.plot}")


if __name__ == "__main__":
    main()
