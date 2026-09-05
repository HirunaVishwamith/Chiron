#!/usr/bin/env python3
"""Figure 5 — one finding, end to end.

  (a) Unperturbed, the workload finishes at 6,629 cycles. With a 343-cycle
      delay of hart 2 it is still running at 100× that length, every hart
      retiring.
  (b) Delta debugging reduces 1,181 hart-cycles of delay to 343, in 20 trials.
      The predicate is a pure function of the schedule, so every trial is
      tested once.
"""
import argparse, csv, json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import chiron_style as cs

HART_COLOR = [cs.CATEGORICAL[5], cs.RANDOM, cs.BASELINE, cs.BRUTE]


def read_timeline(path):
    cyc, ret, stall = [], [[], [], [], []], []
    with open(path) as f:
        for row in csv.DictReader(f):
            cyc.append(int(row["cycle"]))
            stall.append(int(row["stall_mask"]))
            for h in range(4):
                ret[h].append(int(row[f"retired{h}"]))
    return cyc, ret, stall


def build(args):
    cs.use_paper_style()
    fig, (az, bx) = plt.subplots(1, 2, figsize=cs.figsize("find"),
                                 gridspec_kw=dict(width_ratios=[1.20, 1.00]))
    fig.subplots_adjust(left=0.07, right=0.98, bottom=0.20, top=0.86,
                        wspace=0.34)

    cyc, ret, stall = read_timeline(args.timeline)
    bcyc, bret, _ = read_timeline(args.baseline)

    s0 = s1 = held_hart = None
    with open(args.ksched) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            a, b, m = line.split()
            s0, s1 = int(a), int(b)
            held_hart = (int(m, 16) & -int(m, 16)).bit_length() - 1
            break

    # ── (a) where the delay lands, and that the unperturbed run finishes ──
    ZOOM = 9000
    az.axvspan(s0, s1, color=cs.BASELINE, alpha=0.16, lw=0, zorder=0)
    for h in range(4):
        az.plot(cyc, ret[h], lw=1.35 if h == held_hart else 0.85,
                color=HART_COLOR[h], zorder=3 if h == held_hart else 2,
                label=(f"hart {h}  (held)" if h == held_hart else f"hart {h}"))
    az.plot(bcyc, bret[0], lw=1.15, ls=(0, (3.0, 1.6)), color=cs.INK,
            zorder=3, label="unperturbed")
    az.plot([bcyc[-1]], [bret[0][-1]], marker="o", ms=4.2,
            color=cs.STATUS["pass"], zorder=4, clip_on=False)
    az.annotate("completes, 6,629",
                xy=(bcyc[-1], bret[0][-1]), xytext=(7200, 420),
                fontsize=6.4, color=cs.STATUS["pass"], ha="left",
                arrowprops=dict(arrowstyle="-", color=cs.STATUS["pass"],
                                lw=0.7, shrinkA=1, shrinkB=3))
    az.text((s0 + s1) / 2, 2950, f"hart {held_hart}\nheld {s1 - s0}",
            fontsize=6.3, color=cs.BASELINE, ha="center", va="top")
    az.set_xlim(0, ZOOM)
    az.set_ylim(0, 4200)
    az.set_xlabel("cycle")
    az.set_ylabel("instructions committed")
    az.set_title("(a)  a delay the regression cannot reach", loc="left", pad=3)
    az.set_xticks([0, 2000, 4000, 6000, 8000])
    az.set_xticklabels(["0", "2k", "4k", "6k", "8k"])
    az.legend(fontsize=6.0, loc="upper left", handlelength=1.2,
              borderpad=0.2, labelspacing=0.2, ncol=2, columnspacing=0.9,
              framealpha=0.92)

    # The caption's "still retiring at 100x that length" is invisible on a
    # 9,000-cycle axis. An inset carries the whole run, so the claim is shown
    # rather than asserted.
    ins = az.inset_axes([0.520, 0.505, 0.455, 0.415], facecolor="white")
    ins.set_zorder(6)
    ins.patch.set_alpha(1.0)
    for h in range(4):
        ins.plot(cyc, ret[h], lw=0.7, color=HART_COLOR[h])
    ins.plot(bcyc, bret[0], lw=0.9, ls=(0, (2.4, 1.4)), color=cs.INK)
    ins.axvline(bcyc[-1], color=cs.STATUS["pass"], lw=0.7)
    ins.annotate("unperturbed ends here", xy=(bcyc[-1], 0),
                 xytext=(cyc[-1] * 0.20, ins.get_ylim()[1] * 0.62),
                 fontsize=5.2, color=cs.STATUS["pass"], ha="left", va="center",
                 arrowprops=dict(arrowstyle="-", lw=0.5,
                                 color=cs.STATUS["pass"], shrinkA=1, shrinkB=1))
    ins.set_xlim(0, cyc[-1])
    ins.set_xticks([0, 300000, 600000])
    ins.set_xticklabels(["0", "300k", "600k"], fontsize=5.2)
    ins.set_yticks([0, 80000, 160000])
    ins.set_yticklabels(["0", "80k", "160k"], fontsize=5.2)
    ins.tick_params(axis="y", length=1.5, pad=1)
    ins.tick_params(axis="x", length=1.5, pad=1)
    ins.set_title("whole run: all 4 harts still retiring at 662,901",
                  fontsize=5.4, pad=1.5, loc="left", color=cs.INK_MUTED)
    ins.grid(False)
    for sp in ins.spines.values():
        sp.set_linewidth(0.5)
    az.grid(axis="y", alpha=0.30, lw=0.5)
    az.grid(axis="x", visible=False)

    # ── (b) the reduction ──────────────────────────────────────────────────
    with open(args.shrink) as f:
        rep = json.load(f)
    rec = next(r for r in rep["runs"] if r.get("shrink_trace"))
    tr = rec["shrink_trace"]
    sh = rec["shrink"]

    xs = [t["i"] for t in tr]
    ys = [t["cycles"] for t in tr]
    ok = [t["repro"] for t in tr]

    bx.step(xs, ys, where="post", color=cs.INK_MUTED, lw=0.9, zorder=1)
    bx.scatter([x for x, o in zip(xs, ok) if o],
               [y for y, o in zip(ys, ok) if o],
               s=16, color=cs.OURS, zorder=3, label="reproduces", linewidths=0)
    bx.scatter([x for x, o in zip(xs, ok) if not o],
               [y for y, o in zip(ys, ok) if not o],
               s=16, facecolor="white", edgecolor=cs.INK_MUTED, linewidths=0.9,
               zorder=3, label="does not")
    bx.axhline(sh["cycles_before"], color=cs.BASELINE, lw=0.8, ls=":")
    bx.axhline(sh["cycles_after"], color=cs.OURS, lw=0.8, ls=":")
    bx.text(0.6, sh["cycles_before"] + 18, f'{sh["cycles_before"]}',
            ha="left", va="bottom", fontsize=6.5, color=cs.BASELINE)
    bx.text(len(xs) - 0.2, sh["cycles_after"] - 18, f'{sh["cycles_after"]}',
            ha="right", va="top", fontsize=6.5, color=cs.OURS)
    bx.set_xlabel("delta-debugging trial")
    bx.set_ylabel("hart-cycles held")
    bx.set_title("(b)  reduction is exact", loc="left", pad=3)
    bx.set_xlim(0.4, len(xs) + 0.6)
    bx.set_ylim(200, 1300)
    bx.legend(fontsize=6.3, loc="center right", handlelength=1.1,
              borderpad=0.2, labelspacing=0.25)
    bx.grid(axis="y", alpha=0.30, lw=0.5)
    bx.grid(axis="x", visible=False)

    os.makedirs(args.outdir, exist_ok=True)
    print(cs.save(fig, os.path.join(args.outdir, "fig_finding")))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeline", default="build/kairos/tl_full.csv")
    ap.add_argument("--baseline", default="build/kairos/tl_baseline.csv")
    ap.add_argument("--shrink", default="build/kairos/shrink-trace.json")
    ap.add_argument("--ksched", default="build/kairos/spinwait-min.ksched")
    ap.add_argument("--outdir", default="paper/dac27/figures")
    build(ap.parse_args())
