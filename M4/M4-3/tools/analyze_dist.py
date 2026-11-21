#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_dist.py —— M4-3 任务 4：抗扰实验（手捏停轮子）对比

输入：各控制模式下手捏扰动的 STEP 日志
输出：跌落幅度、恢复时间、恢复后稳态误差、反冲峰值 + 归一化叠图

用法：
  python analyze_dist.py ../data/dist_P.txt ../data/dist_PI.txt \
                         ../data/dist_PIFF.txt ../data/dist_PID.txt \
                         --labels P,PI,PI+FF,PID \
                         --plot ../docs/plots/dist_reject.png
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_step import parse, segments  # noqa: E402


def analyse(rows):
    """从一段"目标恒定"的数据里提取扰动响应指标。"""
    t = np.array([r[0] for r in rows], dtype=float) - rows[0][0]
    rpm = np.array([r[2] for r in rows], dtype=float)
    tgt = rows[0][1]

    # 基线：前 1.5 秒的均值（捏之前已经稳定）
    base_mask = t < 1500.0
    if not base_mask.any():
        base_mask = np.zeros_like(t, dtype=bool)
        base_mask[:max(5, len(t) // 5)] = True
    base = float(np.mean(rpm[base_mask]))

    # 扰动段：1.5 秒之后转速跌破基线 50% 的区域
    sgn = 1.0 if tgt >= 0 else -1.0
    after = t >= 1500.0
    idx = np.where(after & (rpm * sgn < base * sgn * 0.5))[0]
    if len(idx) == 0:
        return None

    i_dip_start = idx[0]
    # 恢复点：跌落开始后第一次回到基线 95% 以上（且此后不再跌破 80%）
    i_rec = None
    for i in range(i_dip_start, len(rpm)):
        if rpm[i] * sgn >= base * sgn * 0.95:
            tail = rpm[i:] * sgn
            if np.all(tail >= base * sgn * 0.80):
                i_rec = i
                break

    dip_low = float(rpm[idx[0]: (i_rec if i_rec else len(rpm))].min() * sgn)
    peak_rebound = float(rpm[i_dip_start:].max() * sgn)

    # 恢复后稳态（最后 20%）
    n = max(4, int(len(rpm) * 0.2))
    tail_mean = float(np.mean(rpm[-n:]))

    return {
        "tgt": tgt,
        "base": base,
        "dip_low": dip_low,
        "drop_pct": (base - dip_low) / abs(base) * 100.0 if abs(base) > 1e-6 else 0.0,
        "t_dip": float(t[i_dip_start]),
        "t_rec": (float(t[i_rec] - t[i_dip_start]) if i_rec else float("nan")),
        "rebound_pct": (peak_rebound - base) / abs(base) * 100.0 if abs(base) > 1e-6 else 0.0,
        "err_after_pct": (tail_mean - tgt) / abs(tgt) * 100.0 if abs(tgt) > 1e-6 else 0.0,
        "t": t, "rpm": rpm,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--labels", default=None)
    ap.add_argument("--plot", default=None)
    ap.add_argument("--max-t", type=int, default=30000)
    args = ap.parse_args()

    labels = (args.labels.split(",") if args.labels
              else [os.path.splitext(os.path.basename(f))[0] for f in args.files])

    results = []
    for f, lab in zip(args.files, labels):
        rows = [r for r in parse(f) if r[0] <= args.max_t]
        if not rows:
            print(f"[WARN] {f} 无数据")
            continue
        # 取最长的那一段恒定目标（就是录制的稳态段）
        segs = [s for s in segments(rows) if len(s) >= 20]
        if not segs:
            print(f"[WARN] {f} 没有足够长的稳态段")
            continue
        seg = max(segs, key=len)
        m = analyse(seg)
        if m is None:
            print(f"[WARN] {lab}: 没检测到扰动（转速没跌破基线一半）")
            continue
        m["label"] = lab
        results.append(m)

    print(f"{'controller':>10} {'target':>7} {'base':>7} {'dip':>7} "
          f"{'drop%':>7} {'recover_ms':>11} {'rebound%':>9} {'err_after%':>10}")
    print("-" * 80)
    for m in results:
        print(f"{m['label']:>10} {m['tgt']:>7.1f} {m['base']:>7.2f} "
              f"{m['dip_low']:>7.2f} {m['drop_pct']:>7.1f} "
              f"{m['t_rec']:>11.0f} {m['rebound_pct']:>9.1f} "
              f"{m['err_after_pct']:>10.2f}")

    if args.plot and results:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, ax = plt.subplots(figsize=(11, 5.5))
        for m in results:
            norm = m["rpm"] / abs(m["base"]) * 100.0 * (1 if m["base"] >= 0 else -1)
            ax.plot(m["t"], norm, lw=1.3, label=f"{m['label']} (base {m['base']:.0f} rpm)")
        ax.axhline(100, ls="--", lw=1.0, color="k", alpha=0.6)
        ax.axhline(0, ls=":", lw=0.8, color="r", alpha=0.6)
        ax.set_xlabel("time (ms)")
        ax.set_ylabel("speed (% of baseline)")
        ax.set_title("Disturbance rejection: pinch the wheel and release (all normalized)")
        ax.grid(alpha=0.3)
        ax.legend()
        os.makedirs(os.path.dirname(os.path.abspath(args.plot)), exist_ok=True)
        fig.tight_layout()
        fig.savefig(args.plot, dpi=140)
        print(f"\n[PLOT] saved {args.plot}")


if __name__ == "__main__":
    main()
