#!/usr/bin/env python3
"""RTL hook: one AND into an existing handshake. Labels sit in empty space."""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib.pyplot as plt

import chiron_style as cs
import diagram as dg

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")


def build(outdir):
    cs.use_paper_style()
    fig, ax = plt.subplots(figsize=(3.33, 2.55))
    fig.subplots_adjust(left=0.04, right=0.96, bottom=0.06, top=0.96)
    dg.blank_axes(ax)

    y, h = 0.66, 0.20
    dg.box(ax, 0.03, y, 0.26, h, "Fetch", "rtl", "I-cache · BTB", 7.6)
    dg.box(ax, 0.50, y, 0.22, h, "Decode", "rtl", "rename", 7.6)
    dg.box(ax, 0.76, y, 0.21, h, "Backend", "rtl", "ROB", 7.6)

    ymid = y + h / 2
    gx = 0.385
    dg.and_gate(ax, gx, ymid, r=0.032)
    dg.arrow(ax, (0.29, ymid), (gx - 0.034, ymid), kind="rtl", lw=1.05)
    dg.arrow(ax, (gx + 0.034, ymid), (0.50, ymid), kind="rtl", lw=1.05)
    dg.arrow(ax, (0.72, ymid), (0.76, ymid), kind="rtl", lw=1.05)

    # ready/valid from decode, well below the datapath
    yrv = 0.50
    dg.arrow(ax, (0.61, y), (0.61, yrv), kind="rtl", style="-", lw=0.75)
    dg.arrow(ax, (0.61, yrv), (gx, yrv), kind="rtl", style="-", lw=0.75)
    dg.arrow(ax, (gx, yrv), (gx, ymid - 0.032), kind="rtl", lw=0.75)
    dg.note(ax, 0.64, yrv - 0.015, "ready & valid", fontsize=6.2)

    dg.box(ax, 0.08, 0.18, 0.42, 0.18, "scheduleStall[h]", "hook",
           "1 bit, from Kairos", 7.2)
    dg.bubble(ax, gx - 0.018, ymid - 0.048, r=0.011)
    dg.arrow(ax, (0.29, 0.36), (gx - 0.018, ymid - 0.060), kind="hook", lw=1.05)

    dg.arrow(ax, (0.865, y), (0.865, 0.28), kind="kairos", lw=0.85)
    dg.note(ax, 0.88, 0.22, "retire", fontsize=6.3, color=dg.EDGE["kairos"],
            ha="left")

    dg.note(ax, 0.03, 0.05,
            "Held high  =  fetch not ready.  Delay only.",
            fontsize=6.4, color=cs.INK)

    os.makedirs(outdir, exist_ok=True)
    print(cs.save(fig, os.path.join(outdir, "fig3_hook")))


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default=os.path.join(ROOT, "paper/dac27/figures"))
    build(ap.parse_args().outdir)
