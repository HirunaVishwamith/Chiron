#!/usr/bin/env python3
"""Time-to-exposure, in the style of ProcessorFuzz Table 1 / Cascade Fig. 18.

Same DUT, same findings, four ways of looking. Deterministic regression never
sees them. Random delay did not in the 8-run budget. Windowed Kairos finds
them in a few thousand cycles. The Linux boot found a *different* (RTL)
coherence bug by brute force at 8.7e8 cycles — shown as the brute-force
reference, not as the same bug.
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from chiron_style import (BASELINE, RANDOM, OURS, BRUTE, INK, INK_MUTED,
                          save, use_paper_style, figsize)

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")

# Cycles of RTL simulation until the finding is first reported.
# "never" is plotted at a sentinel and labelled.
NEVER = 1e12
LABELS = [
    "deterministic\nregression",
    "random delay\n(UVM-style)",
    "Kairos\nwindowed",
    "Linux boot\n(brute force)",
]
# Finding 2: 4 runs of a 1,404-cycle binary. Finding 1: 8 runs, hit on seed 3
# of a 6,629-cycle binary. Use the smaller (stronger) number for Kairos.
VALUES = [NEVER, NEVER, 4 * 1404, 8.7e8]
COLORS = [BASELINE, RANDOM, OURS, BRUTE]
HATCH = ["////", "////", None, None]


def main():
    use_paper_style()
    fig, ax = plt.subplots(figsize=figsize("single"))
    fig.subplots_adjust(left=0.22, right=0.97, bottom=0.28, top=0.88)
    x = np.arange(len(LABELS))
    plot_vals = [v if v < NEVER else 3e11 for v in VALUES]
    bars = ax.bar(x, plot_vals, color=COLORS, width=0.62, edgecolor="none")
    for b, h in zip(bars, HATCH):
        if h:
            b.set_hatch(h)
            b.set_edgecolor("white")
            b.set_linewidth(0.4)
    ax.set_yscale("log")
    ax.set_ylabel("RTL cycles until first finding")
    ax.set_xticks(x)
    ax.set_xticklabels(LABELS, fontsize=6.4)
    ax.set_ylim(1e3, 2e12)
    ax.set_yticks([1e3, 1e5, 1e7, 1e9])
    ax.grid(axis="x", visible=False)
    ax.text(0, 4.2e11, "never", ha="center", va="bottom",
            fontsize=6.5, color=BASELINE)
    ax.text(1, 4.2e11, "never", ha="center", va="bottom",
            fontsize=6.5, color=RANDOM)
    ax.text(2, 4 * 1404 * 2.2, "5.6k", ha="center", va="bottom",
            fontsize=6.6, color=OURS, fontweight="bold")
    ax.text(3, 8.7e8 * 1.8, "8.7e8", ha="center", va="bottom",
            fontsize=6.6, color=BRUTE, fontweight="bold")
    ax.set_title("same DUT, four ways of looking", loc="left", fontsize=8.0)
    out = save(fig, os.path.join(ROOT, "paper/dac27/figures/fig_tte"))
    print("wrote", out)


if __name__ == "__main__":
    main()
