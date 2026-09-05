#!/usr/bin/env python3
"""Figure 6 — two orthogonal axes.

Almost all dynamic hardware verification varies the program and observes the
architectural result. Kairos varies the schedule of a fixed program and
observes microarchitectural state. Those multiply; they do not compete.
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

from chiron_style import (CATEGORICAL, INK, INK_MUTED, OURS, GRID,
                          save, use_paper_style, figsize)

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")

CELLS = [
    # (col, row, title, body, ours)
    (0, 1, "ISA fuzzers\nDiffTest  ·  lockstep",
     "program in, architectural\nstate out", False),
    (1, 1, "rare",
     "a schedule change alone\nrarely changes a legal result", False),
    (0, 0, "DifuzzRTL  ·  RFUZZ\nCascade  ·  TheHuzz",
     "mostly single-core;\ncoverage of RTL muxes", False),
    (1, 0, "Kairos",
     "fix the program,\nvary the schedule,\nobserve the microarchitecture", True),
]


def main():
    use_paper_style()
    fig, ax = plt.subplots(figsize=figsize("pos"))
    fig.subplots_adjust(left=0.20, right=0.97, bottom=0.08, top=0.86)
    ax.set_xlim(0, 2)
    ax.set_ylim(0, 2)
    ax.set_xticks([0.5, 1.5])
    ax.set_xticklabels(["varies the program", "varies the schedule"],
                       fontsize=7.0)
    ax.set_yticks([0.5, 1.5])
    ax.set_yticklabels(["observes\nmicroarchitecture",
                        "observes\narchitectural result"], fontsize=7.0)
    ax.tick_params(length=0)
    ax.set_axisbelow(True)
    for s in ax.spines.values():
        s.set_color(GRID)
        s.set_linewidth(0.6)
    ax.axvline(1.0, color=GRID, lw=0.6)
    ax.axhline(1.0, color=GRID, lw=0.6)
    ax.grid(False)

    pad = 0.06
    for col, row, title, body, ours in CELLS:
        x, y = col + pad, row + pad
        w, h = 1 - 2 * pad, 1 - 2 * pad
        face = "#E8F1F8" if ours else "#FAFAFA"
        edge = OURS if ours else GRID
        lw = 1.3 if ours else 0.6
        ax.add_patch(FancyBboxPatch(
            (x, y), w, h, boxstyle="round,pad=0,rounding_size=0.04",
            facecolor=face, edgecolor=edge, linewidth=lw, zorder=2))
        ax.text(col + 0.5, row + 0.70, title, ha="center", va="center",
                fontsize=7.0, color=INK, fontweight="bold" if ours else "normal",
                linespacing=1.25, zorder=3)
        ax.text(col + 0.5, row + 0.32, body, ha="center", va="center",
                fontsize=6.2, color=INK_MUTED, linespacing=1.30, zorder=3)

    ax.set_title("two axes, not two competitors", loc="left", fontsize=8.0, pad=6)
    out = save(fig, os.path.join(ROOT, "paper/dac27/figures/fig6_position"))
    print("wrote", out)


if __name__ == "__main__":
    main()
