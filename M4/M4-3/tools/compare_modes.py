#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
compare_modes.py —— M4-3 任务 3：P / PI / PI+前馈 / PID 四种控制结构的阶跃对比

复用 analyze_step.py 的解析与指标计算，把多组数据并排成一张表 + 叠图。

用法：
  python compare_modes.py ../data/step_P.txt ../data/step_PI.txt \
                          ../data/step_test.txt ../data/step_PID.txt \
                          --labels P,PI,PI+FF,PID \
                          --plot ../docs/plots/mode_compare.png
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_step import parse, segments  # noqa: E402

SETTLE_BAND = 0.02
RIPPLE_TAIL = 0.4


def seg_metrics(seg, tgt_want):
    """算某一段（目标为目标值的段）的指标。"""
    t = np.array([r[0] for r in seg], dtype=float)
    rpm = np.array([r[2] for r in seg], dtype=float)
    pwm = np.array([r[3] for r in seg], dtype=float)
    t = t - t[0]

    tail = max(3, int(len(rpm) * 0.3))
    final = float(np.mean(rpm[-tail:]))

    out = {
        "tgt": tgt_want,
        "final": final,
        "err_pct": (final - tgt_want) / abs(tgt_want) * 100.0,
        "pwm": float(np.mean(pwm[-tail:])),
    }

    # 上升时间用"终值的 10%→90%"，这样 P 组（永远到不了目标）也能算
    if abs(final) < 1e-3:
        out["rise"] = None
    else:
        lo, hi = 0.1 * final, 0.9 * final
        sgn = np.sign(final)
        i_lo = np.where((rpm - lo) * sgn >= 0)[0]
        i_hi = np.where((rpm - hi) * sgn >= 0)[0]
        out["rise"] = (float(t[i_hi[0]] - t[i_lo[0]])
                       if len(i_lo) and len(i_hi) else None)

    # 超调：只有真正越过目标才算
    peak = float(rpm[np.argmax(np.abs(rpm) * (np.sign(rpm) == np.sign(tgt_want)))]) \
        if np.any(np.sign(rpm) == np.sign(tgt_want)) else 0.0
    if abs(tgt_want) > 1e-6 and abs(peak) > abs(tgt_want):
        out["overshoot"] = (abs(peak) - abs(tgt_want)) / abs(tgt_want) * 100.0
    else:
        out["overshoot"] = 0.0
    out["peak"] = peak

    # 调节时间：判定带 max(±2% 终值, ±2 rpm)（2 rpm 是量化下限）
    band = max(SETTLE_BAND * abs(final), 2.0) if abs(final) > 1e-3 else 2.0
    inside = np.abs(rpm - final) <= band
    out["settle"] = 0.0
    for i in range(len(inside) - 1, -1, -1):
        if not inside[i]:
            out["settle"] = float(t[min(i + 1, len(t) - 1)])
            break

    n = max(5, int(len(rpm) * RIPPLE_TAIL))
    out["ripple"] = float(rpm[-n:].max() - rpm[-n:].min())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--labels", default=None, help="逗号分隔，缺省用文件名")
    ap.add_argument("--plot", default=None)
    ap.add_argument("--max-t", type=int, default=20000)
    args = ap.parse_args()

    labels = (args.labels.split(",") if args.labels
              else [os.path.splitext(os.path.basename(f))[0] for f in args.files])
    if len(labels) != len(args.files):
        raise SystemExit("--labels 数量要和文件数一致")

    curves = {}
    rows_pos, rows_neg = [], []

    for f, lab in zip(args.files, labels):
        rows = [r for r in parse(f) if r[0] <= args.max_t]
        if not rows:
            print(f"[WARN] {f} 没有数据，跳过")
            continue
        segs = segments(rows)
        curves[lab] = rows
        for tgt in (60.0, -60.0):
            hit = [s for s in segs if len(s) >= 5 and abs(s[0][1] - tgt) < 1e-6]
            if not hit:
                continue
            m = seg_metrics(hit[0], tgt)
            m["label"] = lab
            (rows_pos if tgt > 0 else rows_neg).append(m)

    def dump(title, table):
        print(f"\n{title}")
        print(f"{'controller':>10} {'final':>8} {'err%':>8} {'rise_ms':>8} "
              f"{'over%':>7} {'settle_ms':>10} {'ripple':>7} {'pwm':>6}")
        print("-" * 72)
        for m in table:
            rise = f"{m['rise']:.0f}" if m["rise"] is not None else "-"
            print(f"{m['label']:>10} {m['final']:>8.2f} {m['err_pct']:>8.2f} "
                  f"{rise:>8} {m['overshoot']:>7.2f} {m['settle']:>10.0f} "
                  f"{m['ripple']:>7.2f} {m['pwm']:>6.0f}")

    dump("== +60 rpm 阶跃 ==", rows_pos)
    dump("== -60 rpm 阶跃 ==", rows_neg)

    if args.plot and curves:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, axes = plt.subplots(1, 2, figsize=(13, 5.2))
        for ax, tgt in zip(axes, (60.0, -60.0)):
            for lab, rows in curves.items():
                # 找到该目标段起点
                segs = segments(rows)
                hit = [s for s in segs if len(s) >= 5 and abs(s[0][1] - tgt) < 1e-6]
                if not hit:
                    continue
                seg = hit[0]
                t0 = seg[0][0]
                ts = [r[0] - t0 for r in seg]
                ys = [r[2] for r in seg]
                ax.plot(ts, ys, lw=1.3, label=lab)
            ax.axhline(tgt, ls="--", lw=1.0, color="k", alpha=0.6)
            ax.set_title(f"step to {tgt:+.0f} rpm")
            ax.set_xlabel("time since step (ms)")
            ax.set_ylabel("rpm")
            ax.grid(alpha=0.3)
            ax.legend()
        os.makedirs(os.path.dirname(os.path.abspath(args.plot)), exist_ok=True)
        fig.tight_layout()
        fig.savefig(args.plot, dpi=140)
        print(f"\n[PLOT] saved {args.plot}")


if __name__ == "__main__":
    main()
