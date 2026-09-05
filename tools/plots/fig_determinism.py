#!/usr/bin/env python3
"""Figure: RTL multicore simulation is deterministic, so reverting a real
coherence bug changes nothing observable.

Data measured 2026-09-05 (docs/research/06, addendum): swmr_probe tag-state
change counts over 6M cycles per workload, on the fixed model and on the model
with bc4a4ab (the dual-Unique fix) reverted. The counts are identical to the
digit -- the buggy and fixed designs execute the same schedule, so the bug is
never activated.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import matplotlib.pyplot as plt
from chiron_style import use_paper_style, figsize, save, OURS, BASELINE, INK_MUTED

use_paper_style()

WORKLOADS = ["mt-fencei", "mt-crosscall", "mt-llist", "mt-seqlock", "mt-lrsc"]
FIXED = [4091, 4158, 26090, 160148, 139716]
BUGGY = [4091, 4158, 26090, 160148, 139716]   # identical, and that is the point

x = np.arange(len(WORKLOADS))
w = 0.38

fig, ax = plt.subplots(figsize=figsize("single"))
ax.bar(x - w/2, FIXED, w, label="fixed RTL",             color=OURS)
ax.bar(x + w/2, BUGGY, w, label="dual-Unique reverted",  color=BASELINE)

ax.set_yscale("log")
ax.set_ylabel("cache tag-state transitions (log scale)")
ax.set_xticks(x)
ax.set_xticklabels(WORKLOADS, rotation=20, ha="right")
ax.legend(loc="upper left", ncol=1, handlelength=1.1, borderaxespad=0.2)

# The finding is the equality, so state it on the figure rather than making the
# reader compare bar heights that are identical by construction.
# Place the note clear of the legend (upper-left) and point it at a pair whose
# equality is easiest to read.
ax.annotate("every pair identical to the digit:\nthe bug is never activated",
            xy=(3.0, 160148), xytext=(2.35, 1.6e6),
            fontsize=6.5, color=INK_MUTED, ha="left",
            arrowprops=dict(arrowstyle="->", color=INK_MUTED, lw=0.6,
                            connectionstyle="arc3,rad=-0.15"))
ax.set_ylim(top=3.0e7)

print(save(fig, "paper/dac27/figures/fig_determinism"))
