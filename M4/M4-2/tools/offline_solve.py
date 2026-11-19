#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
offline_solve.py —— M4-2 离线复算脚本（现场用）

现场会给出**没见过的** IMU 原始数据流，要求用它跑自己的融合算法，
输出角度曲线并与参考真值比对（误差 ≤5° 为通过）。

输入 CSV 约定：t, ax, ay, az, gx, gy, gz
  t  秒（相对时间，可以是任意起点）
  ax/ay/az  单位 g
  gx/gy/gz  单位 °/s

输出 CSV：t, angle_deg

关键约束（题目明确要求）：
  * **不许硬编码零偏** —— 必须从数据自身的前段静止区间估计
  * 不依赖特定硬件的绝对时间 —— 只用 t 列的差值
  * 只依赖 numpy（csv 用标准库）

用法：
  python offline_solve.py --input imu_raw.csv --out solved.csv
  python offline_solve.py --input imu_raw.csv --out solved.csv --tau 0.5 --verbose
"""
import argparse
import csv
import math
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("missing dependency: pip install numpy")


# 列名兼容：现场数据的列名不一定和我们的一样
ALIASES = {
    "t":   ("t", "time", "timestamp", "ts", "sec", "second"),
    "ax":  ("ax", "ax_g", "acc_x", "accel_x"),
    "ay":  ("ay", "ay_g", "acc_y", "accel_y"),
    "az":  ("az", "az_g", "acc_z", "accel_z"),
    "gx":  ("gx", "gx_dps", "gyro_x", "gyr_x", "wx"),
    "gy":  ("gy", "gy_dps", "gyro_y", "gyr_y", "wy"),
    "gz":  ("gz", "gz_dps", "gyro_z", "gyr_z", "wz"),
}


def pick_columns(header):
    """按别名表找到每一列的下标，返回 dict；缺列返回 None"""
    norm = [h.strip().lower().lstrip("\ufeff") for h in header]
    idx = {}
    for key, names in ALIASES.items():
        idx[key] = None
        for i, h in enumerate(norm):
            if h in names:
                idx[key] = i
                break
    return idx


def load(path):
    with open(path, newline="", encoding="utf-8", errors="ignore") as fh:
        # 有些数据文件前面有注释行或空行，跳过
        lines = [ln for ln in fh if ln.strip() and not ln.lstrip().startswith("#")]
    if not lines:
        sys.exit("input file is empty")

    header = next(csv.reader([lines[0]]))
    idx = pick_columns(header)
    missing = [k for k, v in idx.items() if v is None]
    if missing:
        sys.exit(f"missing required columns: {missing}\nheader was: {header}")

    rows = []
    for ln in lines[1:]:
        parts = next(csv.reader([ln]))
        try:
            rows.append([float(parts[idx[k]]) for k in ("t", "ax", "ay", "az", "gx", "gy", "gz")])
        except (ValueError, IndexError):
            continue                      # 跳过坏行
    if len(rows) < 20:
        sys.exit(f"too few valid rows: {len(rows)}")
    return np.array(rows, dtype=np.float64)


def estimate_bias(gyro_y, t, static_seconds=1.0, min_samples=20):
    """从前段静止区间估计俯仰轴零偏

    做法：取开头 static_seconds 秒（至少 min_samples 个点），
    用中位数抗野值。不写死任何数值。
    """
    t0 = t[0]
    mask = (t - t0) <= static_seconds
    seg = gyro_y[mask]
    if len(seg) < min_samples:
        seg = gyro_y[:min_samples]
    return float(np.median(seg))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="原始 IMU CSV")
    ap.add_argument("--out", required=True, help="输出 CSV：t,angle_deg")
    ap.add_argument("--tau", type=float, default=0.5, help="互补滤波时间常数（秒），默认 0.5")
    ap.add_argument("--static-seconds", type=float, default=1.0,
                    help="用于估计零偏的开头静止时长（秒）")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    d = load(args.input)
    t, ax, ay, az, gx, gy, gz = (d[:, i] for i in range(7))

    bias = estimate_bias(gy, t, args.static_seconds)
    pitch_acc = np.degrees(np.arctan2(-ax, az))     # 绕 Y 轴 = 俯仰

    # 互补滤波：pitch = α(pitch + (gy-bias)dt) + (1-α)pitch_acc
    n = len(t)
    fused = np.empty(n)
    fused[0] = pitch_acc[0]
    for i in range(1, n):
        dt = t[i] - t[i - 1]
        if dt <= 0.0 or dt > 0.5:
            dt = 1.0 / 100.0                        # 异常间隔兜底
        alpha = args.tau / (args.tau + dt)
        rate = gy[i] - bias
        fused[i] = alpha * (fused[i - 1] + rate * dt) + (1.0 - alpha) * pitch_acc[i]

    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["t", "angle_deg"])
        for i in range(n):
            w.writerow([f"{t[i]:.6f}", f"{fused[i]:.4f}"])

    print(f"[INFO] samples = {n}, duration = {t[-1] - t[0]:.2f}s, "
          f"rate ~ {n / max(1e-9, t[-1] - t[0]):.1f} Hz")
    print(f"[INFO] auto-estimated gyro bias (pitch axis) = {bias:.3f} deg/s")
    print(f"[INFO] fused angle: first {fused[0]:.2f} deg, last {fused[-1]:.2f} deg, "
          f"change {fused[-1] - fused[0]:+.2f} deg")
    print(f"[INFO] accel angle std = {np.std(pitch_acc):.2f} deg (noise + motion)")
    print(f"[INFO] written to {args.out}")

    if args.verbose:
        print(f"[DBG] ax [{ax.min():.3f},{ax.max():.3f}] g, "
              f"ay [{ay.min():.3f},{ay.max():.3f}] g, "
              f"az [{az.min():.3f},{az.max():.3f}] g")
        print(f"[DBG] gy [{gy.min():.2f},{gy.max():.2f}] deg/s")


if __name__ == "__main__":
    main()

