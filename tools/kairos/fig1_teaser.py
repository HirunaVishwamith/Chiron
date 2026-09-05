#!/usr/bin/env python3
"""Figure 1 — the claim, measured.

Two panels, one workload (mt-spinwait), no cartoons:

  (a) Repeating a deterministic RTL simulation buys nothing. Eight runs of
      the same binary yield one interleaving. Under schedule exploration the
      same eight runs yield up to eight.
  (b) The reduced failing schedule is a single contiguous delay of one named
      hart — a file, not a seed.
"""
import json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

from chiron_style import (POLICY_COLOR, POLICY_LABEL, GRID, INK, INK_MUTED,
                          BASELINE, save, use_paper_style, figsize)

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
D = lambda *p: os.path.join(ROOT, *p)

FAM = lambda spec: spec.split(":", 1)[0]
ORDER = ["deterministic", "random:0.02", "windowed:2000", "pct:3"]


def cumulative_distinct(runs, policy):
    seen, out = set(), []
    for r in runs:
        if r["policy"] != policy:
            continue
        seen.add(r["digest"])
        out.append(len(seen))
    return out


def panel_a(ax, rep):
    n = 8
    ax.plot([1, n], [1, n], color=GRID, linewidth=0.9, linestyle=(0, (2.4, 1.8)),
            zorder=1, label="ideal (1 new / run)")

    for pol in ORDER:
        y = cumulative_distinct(rep["runs"], pol)
        fam = FAM(pol)
        # A policy whose runs all hit the cycle budget explored schedules it
        # never finished. Say so on the curve rather than in a footnote --
        # this is the paper's headline plot.
        mine = [r for r in rep["runs"] if r["policy"] == pol]
        all_cut = bool(mine) and all(r.get("verdict") == "timeout" for r in mine)
        lab = POLICY_LABEL[fam] + (" (never finished)" if all_cut else "")
        ax.plot(range(1, len(y) + 1), y,
                marker="o" if fam == "deterministic" else "s",
                markersize=4.0 if fam == "deterministic" else 3.4,
                markerfacecolor="white" if all_cut else POLICY_COLOR[fam],
                markeredgecolor=POLICY_COLOR[fam],
                markeredgewidth=0.9,
                color=POLICY_COLOR[fam],
                linestyle=(0, (3.2, 1.5)) if all_cut else "solid",
                linewidth=1.7 if fam == "deterministic" else 1.25,
                zorder=4 if fam == "deterministic" else 3,
                label=lab)

    ax.set_xlabel("runs of the same binary")
    ax.set_ylabel("distinct interleavings")
    ax.set_xticks(range(1, n + 1))
    ax.set_xlim(0.7, n + 0.35)
    ax.set_ylim(0.4, n + 0.55)
    ax.legend(loc="upper left", handlelength=1.4, borderpad=0.15,
              labelspacing=0.22, fontsize=6.5)
    ax.set_title("(a)  one binary, many schedules", loc="left", pad=3)
    ax.grid(axis="y", alpha=0.35, lw=0.5)
    ax.grid(axis="x", visible=False)


def panel_b(ax, sched):
    spans, held = [], 0
    for ln in open(D(sched)):
        if ln.startswith("#") or not ln.strip():
            continue
        a, b, m = ln.split()
        spans.append((int(a), int(b), int(m, 16)))
    horizon = 6629
    for h in range(4):
        ax.add_patch(Rectangle((0, h - 0.32), horizon, 0.64,
                               facecolor="#F3F3F3", edgecolor="none", zorder=1))
        ax.text(-80, h, f"hart {h}", fontsize=7.0, color=INK, ha="right",
                va="center")
    for a, b, m in spans:
        for h in range(4):
            if m & (1 << h):
                held += b - a
                ax.add_patch(Rectangle((a, h - 0.32), b - a, 0.64,
                                       facecolor=BASELINE, edgecolor="none",
                                       zorder=3))
                ax.annotate(f"{b - a} cycles at {a}",
                            xy=(b, h), xytext=(b + 420, h),
                            fontsize=6.6, color=BASELINE, va="center",
                            arrowprops=dict(arrowstyle="-", lw=0.6,
                                            color=BASELINE, shrinkA=1,
                                            shrinkB=1))
    ax.set_yticks([])
    ax.set_ylim(-0.7, 3.7)
    ax.invert_yaxis()
    ax.set_xlim(-900, horizon)
    ax.set_xticks([0, 2000, 4000, 6000])
    ax.set_xticklabels(["0", "2k", "4k", "6k"])
    ax.set_xlabel("cycle")
    ax.grid(False)
    for s in ("left", "top", "right"):
        ax.spines[s].set_visible(False)
    ax.set_title(f"(b)  reduced schedule — 1 span, {held} cycles",
                 loc="left", pad=4)


def main():
    use_paper_style()
    rep = json.load(open(D("build/kairos/mt-spinwait.json")))
    fig, axes = plt.subplots(1, 2, figsize=figsize("teaser"),
                             gridspec_kw=dict(width_ratios=[1.05, 1.15]))
    fig.subplots_adjust(left=0.055, right=0.985, bottom=0.20, top=0.88,
                        wspace=0.28)
    panel_a(axes[0], rep)
    panel_b(axes[1], "build/kairos/spinwait-min.ksched")
    out = save(fig, D("paper/dac27/figures/fig1_teaser"))
    print("wrote", out)


if __name__ == "__main__":
    main()
