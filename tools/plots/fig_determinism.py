#!/usr/bin/env python3
"""Figure: RTL multicore simulation is deterministic, so reverting a real
coherence bug changes nothing observable.

Data measured 2026-09-05 (docs/research/06): swmr_probe tag-state change
counts over 6M cycles per workload, on the fixed model and on the model with
the dual-Unique fix reverted. The counts are identical to the digit.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import matplotlib.pyplot as plt
from chiron_style import (use_paper_style, figsize, save, OURS, BASELINE,
                          INK, INK_MUTED)

use_paper_style()

WORKLOADS = ["mt-fencei", "mt-crosscall", "mt-llist", "mt-seqlock", "mt-lrsc"]
FIXED = [4091, 4158, 26090, 160148, 139716]
BUGGY = [4091, 4158, 26090, 160148, 139716]

x = np.arange(len(WORKLOADS))
w = 0.36

fig, ax = plt.subplots(figsize=figsize("single"))
fig.subplots_adjust(left=0.20, right=0.97, bottom=0.24, top=0.90)
b1 = ax.bar(x - w / 2, FIXED, w, label="fixed RTL", color=OURS, edgecolor="none")
b2 = ax.bar(x + w / 2, BUGGY, w, label="bug reverted", color=BASELINE,
            edgecolor="none")

ax.set_yscale("log")
ax.set_ylabel("cache tag-state transitions")
ax.set_xticks(x)
ax.set_xticklabels(WORKLOADS, rotation=18, ha="right", fontsize=6.8)
ax.legend(loc="upper left", ncol=1, handlelength=1.1, borderpad=0.2,
          fontsize=6.6)
ax.set_ylim(2e3, 1.2e6)
ax.grid(axis="x", visible=False)
ax.set_title("identical to the digit", loc="left", fontsize=8.0, pad=3)

# Equality is the finding: mark it on the tallest pair, away from the legend.
ax.annotate("bug never activated",
            xy=(3.18, 160148), xytext=(2.15, 5.5e5),
            fontsize=6.4, color=INK_MUTED, ha="center",
            arrowprops=dict(arrowstyle="-", color=INK_MUTED, lw=0.6,
                            shrinkA=0, shrinkB=2))

print(save(fig, "paper/dac27/figures/fig_determinism"))
