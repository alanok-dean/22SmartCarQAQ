#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_step.py —— M4-3 阶跃响应分析

输入：串口日志（二进制状态帧和文本混在一起也照样能解析，靠正则只捞 STEP 行）
输出：控制性能表 + 阶跃响应曲线 PNG

用法：
  python analyze_step.py ../data/step_test.txt
  python analyze_step.py ../data/step_test.txt --plot ../docs/plots/step_response.png
"""
import argparse
import os
import re

import numpy as np

# STEP,t_ms,target_rpm,rpm,pwm,integ
PAT = re.compile(r"STEP,\s*(-?\d+),\s*(-?[\d.]+),\s*(-?[\d.]+),\s*(-?[\d.]+),\s*(-?[\d.]+)")

SETTLE_BAND = 0.02      # 稳态判定带：终值的 ±2%
RIPPLE_TAIL = 0.4       # 用每段最后 40% 的数据统计稳态纹波


def _read_text(path):
    """读文本，自动处理 BOM。

    PowerShell 的 Tee-Object 默认写 UTF-16LE，直接按 utf-8 读会全是乱码，
    所以先看 BOM，再依次尝试几种编码。
    """
    with open(path, "rb") as fh:
        raw = fh.read()

    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16", errors="ignore")
    if raw[:3] == b"\xef\xbb\xbf":
        return raw[3:].decode("utf-8", errors="ignore")

    for enc in ("utf-8", "gbk", "latin-1"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("latin-1", errors="ignore")


def parse(path):
    """返回 [(t, tgt, rpm, pwm, integ), ...]，按时间排序且去重。"""
    rows = {}
    for line in _read_text(path).splitlines():
        m = PAT.search(line)
        if not m:
            continue
        t = int(m.group(1))
        # 同一次控制周期可能被重复打印，按时间戳去重
        if t in rows:
            continue
        rows[t] = (t,
                   float(m.group(2)),
                   float(m.group(3)),
                   float(m.group(4)),
                   float(m.group(5)))
    return [rows[t] for t in sorted(rows)]


def segments(rows):
    """按 target 变化切段。"""
    segs = []
    cur = []
    cur_tgt = None
    for r in rows:
        if cur_tgt is None or abs(r[1] - cur_tgt) > 1e-6:
            if cur:
                segs.append(cur)
            cur = [r]
            cur_tgt = r[1]
        else:
            cur.append(r)
    if cur:
        segs.append(cur)
    return segs


def metrics(seg):
    """算一段的阶跃指标。"""
    t = np.array([r[0] for r in seg], dtype=float)
    tgt = seg[0][1]
    rpm = np.array([r[2] for r in seg], dtype=float)
    pwm = np.array([r[3] for r in seg], dtype=float)

    # 终值取该段后 30% 的均值，避开阶跃瞬间
    tail = max(3, int(len(rpm) * 0.3))
    final = float(np.mean(rpm[-tail:]))
    # 把段内时间平移成相对量，便于比较
    t = t - t[0]

    out = {
        "tgt": tgt,
        "final": final,
        "pwm_final": float(np.mean(pwm[-tail:])),
        "dur_ms": float(t[-1]),
    }

    if abs(tgt) < 1e-6:
        # 停车段：量"回到 ±0.5rpm 并保持"的时间
        ok = np.where(np.abs(rpm) > 0.5)[0]
        if len(ok) == 0:
            out["t_stop"] = 0.0
        else:
            last_bad = ok[-1]
            out["t_stop"] = float(t[last_bad]) if last_bad >= len(t) - 1 else float(t[last_bad + 1])
        out["overshoot_pct"] = 0.0
        out["t_rise"] = None
        out["t_settle"] = out["t_stop"]
    else:
        # 上升时间：10% -> 90% 终值
        lo, hi = 0.1 * tgt, 0.9 * tgt
        i_lo = np.where((rpm - lo) * np.sign(tgt) >= 0)[0]
        i_hi = np.where((rpm - hi) * np.sign(tgt) >= 0)[0]
        if len(i_lo) and len(i_hi):
            out["t_rise"] = float(t[i_hi[0]] - t[i_lo[0]])
        else:
            out["t_rise"] = None

        # 超调：同号方向的最大偏离
        peak = rpm.max() if tgt > 0 else rpm.min()
        out["overshoot_pct"] = abs(peak - tgt) / abs(tgt) * 100.0
        # 稳态误差（相对终值）
        out["err_pct"] = (final - tgt) / abs(tgt) * 100.0

        # 调节时间：此后一直留在判定带内。
        # 判定带取 max(±2%, ±2 rpm)：10ms 窗口下 1 个计数就是 3.0 rpm，
        # 60 rpm 的 ±2% 只有 1.2 rpm，比一个量化台阶还小，
        # 直接用 2% 会把"测量纹波"误判成"没稳定"。
        band = max(SETTLE_BAND * abs(tgt), 2.0)
        inside = np.abs(rpm - final) <= band
        out["t_settle"] = 0.0
        for i in range(len(inside) - 1, -1, -1):
            if not inside[i]:
                out["t_settle"] = float(t[min(i + 1, len(t) - 1)])
                break

        # 另外给一个 ±5% 的口径，方便和教科书定义对照
        band5 = 0.05 * abs(tgt)
        inside5 = np.abs(rpm - final) <= band5
        out["t_settle5"] = 0.0
        for i in range(len(inside5) - 1, -1, -1):
            if not inside5[i]:
                out["t_settle5"] = float(t[min(i + 1, len(t) - 1)])
                break

    # 稳态纹波：后 40% 数据
    n = max(5, int(len(rpm) * RIPPLE_TAIL))
    tail_rpm = rpm[-n:]
    tail_pwm = pwm[-n:]
    out["rpm_std"] = float(np.std(tail_rpm))
    out["rpm_pp"] = float(tail_rpm.max() - tail_rpm.min())
    out["pwm_pp"] = float(tail_pwm.max() - tail_pwm.min())
    return out


def plot(rows, segs, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t = np.array([r[0] for r in rows], dtype=float)
    tp = [r[1] for r in rows]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 7), sharex=True,
                                   gridspec_kw={"height_ratios": [2, 1]})

    ax1.plot(t, [r[2] for r in rows], lw=1.2, label="measured rpm")
    ax1.step(t, tp, "r--", lw=1.0, label="target rpm", where="post")
    ax1.set_ylabel("speed (rpm)")
    ax1.grid(alpha=0.3)
    ax1.legend(loc="upper right")
    ax1.set_title("M4-3 speed closed loop: step response (10 ms control period)")

    ax2.plot(t, [r[3] for r in rows], lw=1.0, color="tab:green", label="PWM duty")
    ax2.plot(t, [r[4] for r in rows], lw=0.9, color="tab:orange", label="integral term")
    ax2.set_ylabel("PWM / integral")
    ax2.set_xlabel("time from profile start (ms)")
    ax2.grid(alpha=0.3)
    ax2.legend(loc="upper right")

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=140)
    print(f"[PLOT] saved {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--plot", default=None)
    ap.add_argument("--max-t", type=int, default=20000,
                    help="只看这段时间内的数据（阶跃序列本身 15 秒）")
    args = ap.parse_args()

    rows = parse(args.log)
    if not rows:
        raise SystemExit("no STEP line found")
    rows = [r for r in rows if r[0] <= args.max_t]
    if not rows:
        raise SystemExit("no data within --max-t")

    segs = segments(rows)
    print(f"[INFO] {len(rows)} samples, {len(segs)} segments, "
          f"{rows[0][0]}..{rows[-1][0]} ms")
    print()
    print(f"{'target':>8} {'final':>8} {'err%':>6} {'rise_ms':>8} "
          f"{'over%':>6} {'settle2%':>9} {'settle5%':>9} {'ripple':>7} {'pwm_pp':>7}")
    print("-" * 92)
    for seg in segs:
        if len(seg) < 5:
            continue
        m = metrics(seg)
        rise = f"{m['t_rise']:.0f}" if m.get("t_rise") is not None else "-"
        s5 = f"{m['t_settle5']:.0f}" if m.get("t_settle5") is not None else "-"
        print(f"{m['tgt']:>8.1f} {m['final']:>8.2f} "
              f"{(m.get('err_pct') or 0.0):>6.2f} {rise:>8} "
              f"{m['overshoot_pct']:>6.2f} {m['t_settle']:>9.0f} {s5:>9} "
              f"{m['rpm_pp']:>7.2f} {m['pwm_pp']:>7.0f}")

    if args.plot:
        plot(rows, segs, args.plot)


if __name__ == "__main__":
    main()
