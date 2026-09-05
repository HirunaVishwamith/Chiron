#!/usr/bin/env python3
"""Capability matrix of existing tools vs Kairos. PNG for the paper.

Numbers in the companion LaTeX table come from the original papers.
This figure is the qualitative map: what each tool *does*, not who 'wins'.
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle

from chiron_style import (OURS, INK, INK_MUTED, GRID, STATUS,
                          save, use_paper_style)

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")

# Columns: what the tool varies / where it runs / what it observes / reduce
COLS = ["program", "schedule", "MC RTL", "RTL cov.", "liveness", "minimise"]

# 1 = yes, 0 = no, 0.5 = partial (silicon / sim / software)
# "RTL cov." is mux / register / VCS / live cache — not a score.
ROWS = [
    ("RFUZZ",            [1, 0, 0,   1,   0,   0]),
    ("DifuzzRTL",        [1, 0, 0,   1,   0,   0]),
    ("TheHuzz",          [1, 0, 0,   1,   0,   0]),
    ("ProcessorFuzz",    [1, 0, 0,   0,   0,   0]),
    ("Cascade",          [1, 0, 0,   1,   0,   1]),   # mux+register coverage
    ("DiffTest(-H)",     [0, 0, 1,   0,   0,   0]),
    ("TSOtool",          [1, 0, 0.5, 0,   0,   0]),
    ("McVerSi",          [1, 0, 0.5, 0,   0,   0]),
    ("CHESS / PCT",      [0, 1, 0,   0,   1,   1]),
    ("Kairos (this)",    [-1, 1, 1,  1,   1,   1]),   # -1: program held fixed
]


def main():
    use_paper_style()
    n_r, n_c = len(ROWS), len(COLS)
    fig, ax = plt.subplots(figsize=(7.00, 2.55))
    fig.subplots_adjust(left=0.20, right=0.985, bottom=0.03, top=0.93)
    ax.set_xlim(-0.05, n_c)
    ax.set_ylim(n_r, -0.75)
    ax.set_aspect("auto")
    ax.axis("off")

    for j, lab in enumerate(COLS):
        ax.text(j + 0.5, -0.28, lab, ha="center", va="center",
                fontsize=6.5, color=INK_MUTED)

    yes_c, no_c, part_c = OURS, "#F0F0F0", "#F4E4C1"
    for i, (name, vals) in enumerate(ROWS):
        ours = name.startswith("Kairos")
        ax.text(-0.10, i + 0.5, name, ha="right", va="center",
                fontsize=7.0, color=INK,
                fontweight="bold" if ours else "normal")
        for j, v in enumerate(vals):
            if v == 1:
                face, mark, mc, ew = yes_c, "yes", "white", 0.6
            elif v == -1:
                face, mark, mc, ew = no_c, "held fixed", OURS, 0.6
            elif v == 0.5:
                face, mark, mc, ew = part_c, "sim / Si", INK, 0.6
            else:
                face, mark, mc, ew = no_c, "", INK_MUTED, 0.5
            if ours:
                ew = 1.15
            ax.add_patch(FancyBboxPatch(
                (j + 0.08, i + 0.12), 0.84, 0.76,
                boxstyle="round,pad=0,rounding_size=0.08",
                facecolor=face, edgecolor=OURS if ours else GRID,
                linewidth=ew, zorder=2))
            if mark:
                ax.text(j + 0.5, i + 0.5, mark, ha="center", va="center",
                        fontsize=6.0 if v in (0.5, -1) else 6.5,
                        color=mc, fontweight="bold" if v == 1 else "normal",
                        zorder=3)
    out = save(fig, os.path.join(ROOT, "paper/dac27/figures/fig_compare"))
    print("wrote", out)


if __name__ == "__main__":
    main()
