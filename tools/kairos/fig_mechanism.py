#!/usr/bin/env python3
"""Mechanism figure — three visual panels, no overlapping labels.

  (a) the RTL change: one AND into an existing handshake
  (b) the DUT: four cores, CCU, L2; stall is a bus into every hart
  (c) the host loop: policy, oracles, coverage, shrinker

A footer band carries the two measured gates. Captions carry the argument.
No schedule/interleaving prose inside the drawing.
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Circle

import chiron_style as cs
import diagram as dg
from schematic import Canvas

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")


def _vline(ax, x, y0, y1, color, lw=0.85):
    ax.plot([x, x], [y0, y1], color=color, lw=lw, solid_capstyle="butt", zorder=3)


def _hline(ax, x0, x1, y, color, lw=0.85):
    ax.plot([x0, x1], [y, y], color=color, lw=lw, solid_capstyle="butt", zorder=3)


def panel_a(ax):
    """RTL hook. Labels sit in empty space; wires do not cross text."""
    dg.blank_axes(ax)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)

    y, h = 0.70, 0.20
    dg.box(ax, 0.03, y, 0.24, h, "Fetch", "rtl", "I-cache", 7.4)
    dg.box(ax, 0.50, y, 0.22, h, "Decode", "rtl", "rename", 7.4)
    dg.box(ax, 0.76, y, 0.21, h, "Backend", "rtl", "ROB", 7.4)

    ymid = y + h / 2
    gx, gy, gw, gh = 0.33, 0.72, 0.11, 0.16
    dg.box(ax, gx, gy, gw, gh, "&", "hook", None, 9.0, radius=0.03)

    dg.arrow(ax, (0.27, ymid), (gx, ymid), kind="rtl", lw=1.05)
    dg.arrow(ax, (gx + gw, ymid), (0.50, ymid), kind="rtl", lw=1.05)
    dg.arrow(ax, (0.72, ymid), (0.76, ymid), kind="rtl", lw=1.05)

    # ready feedback: drop from Decode, run left *under* the AND, up into it.
    # Horizontal sits at y=0.58, well above the stall box and well below the AND.
    yrv = 0.58
    cx = gx + gw / 2
    _vline(ax, 0.61, y, yrv, dg.EDGE["rtl"], lw=0.75)
    _hline(ax, cx, 0.61, yrv, dg.EDGE["rtl"], lw=0.75)
    dg.arrow(ax, (cx, yrv), (cx, gy), kind="rtl", lw=0.75)
    ax.text(0.63, yrv - 0.018, "ready", fontsize=6.2, color=cs.INK_MUTED,
            ha="left", va="top")

    # stall source under Fetch only, so its arrow never crosses "ready".
    dg.box(ax, 0.03, 0.10, 0.36, 0.22, "scheduleStall", "hook",
           "1 bit / hart", 7.2)
    # Inverter sits on the stall wire, below the AND, so it does not eat the box.
    bubble_x, bubble_y = gx + 0.012, gy - 0.028
    ax.plot([bubble_x, gx + 0.012], [bubble_y + 0.012, gy],
            color=dg.EDGE["hook"], lw=1.05, zorder=3)
    ax.add_patch(Circle((bubble_x, bubble_y), 0.012, facecolor="white",
                        edgecolor=dg.EDGE["hook"], lw=0.9, zorder=5))
    ax.annotate("", xy=(bubble_x, bubble_y),
                xytext=(0.21, 0.32),
                arrowprops=dict(arrowstyle="-", color=dg.EDGE["hook"],
                                lw=1.05, shrinkA=1.5, shrinkB=1.0,
                                connectionstyle="arc3,rad=0.0"),
                zorder=4)
    ax.set_title("(a)  RTL change", loc="left", fontsize=7.4, pad=2)


def panel_b(ax):
    """Four cores + shared L2. A stall bus feeds every hart; no stray labels."""
    c = Canvas(ax, 4.6, 7.2)
    rows = [5.85, 4.55, 3.25, 1.95]
    cores = []
    for i, yy in enumerate(rows):
        cores.append(c.box(1.35, yy, 2.95, title=f"core {i}",
                           body="OoO  ·  L1", role="dut"))
    l2 = c.box(1.35, 0.40, 2.95, title="CCU + L2",
               body="ACE  ·  shared", role="dut")

    # The stall bus reaches the CORES only. Nothing in the coherence
    # fabric is ever held -- drawing an arrow into CCU + L2 would
    # contradict the soundness argument in panel (a).
    bus_x = 0.55
    top = cores[0].left[1]
    bot = cores[-1].left[1]
    ax.plot([bus_x, bus_x], [bot, top], color=dg.EDGE["hook"], lw=1.05, zorder=3)
    for b in cores:
        ax.annotate("", xy=b.left, xytext=(bus_x, b.left[1]),
                    arrowprops=dict(arrowstyle="-|>", color=dg.EDGE["hook"],
                                    lw=0.9, shrinkA=0, shrinkB=1.2),
                    zorder=4)
    ax.text(bus_x, top + 0.28, "stall  (4 b)", fontsize=6.4,
            color=dg.EDGE["hook"], ha="center", va="bottom")
    # cores <-> coherence fabric: untouched, and shown as untouched
    link_x = cores[-1].center[0]
    ax.annotate("", xy=(link_x, l2.top[1]), xytext=(link_x, cores[-1].bottom[1]),
                arrowprops=dict(arrowstyle="<|-|>", color=cs.INK_MUTED,
                                lw=0.8, shrinkA=1.0, shrinkB=1.0), zorder=3)
    ax.text(link_x + 0.12, (l2.top[1] + cores[-1].bottom[1]) / 2,
            "ACE, unperturbed", fontsize=5.9, color=cs.INK_MUTED,
            ha="left", va="center")
    ax.set_title("(b)  DUT", loc="left", fontsize=7.4, pad=2)
    return c


def panel_c(ax):
    c = Canvas(ax, 6.6, 7.2)
    pol = c.box(0.25, 5.55, 2.62, title="Policy",
                body="det · rand · PCT · win", role="ours")
    cov = c.box(3.78, 5.55, 2.62, title="Coverage",
                body="digest · order pairs", role="ours")
    ora = c.box(0.25, 2.85, 2.62, title="Oracles",
                body="result · hang\nlivelock · SWMR", role="ours")
    sh  = c.box(3.78, 3.15, 2.62, title="Shrinker",
                body="delta debugging", role="ours")
    ks  = c.box(3.78, 0.40, 2.62, title=".ksched",
                body="replays exactly", role="artifact")
    # Policy -> DUT -> events -> {coverage, oracles} -> Policy is the loop;
    # the shrinker and the schedule file hang off a verdict, once.
    c.arrow(pol.right, cov.left, label="mask", label_side="below")
    c.arrow(cov.bottom, sh.top, label="events")
    c.arrow(pol.bottom, ora.top)
    c.arrow(ora.right, sh.left, label="verdict", label_side="below")
    c.arrow(sh.bottom, ks.top)
    # close the loop: progress feedback from the oracles back to the policy
    ax.annotate("", xy=(pol.left[0] - 0.16, pol.left[1]),
                xytext=(ora.left[0] - 0.16, ora.left[1]),
                arrowprops=dict(arrowstyle="-|>", color=cs.INK_MUTED, lw=0.8,
                                linestyle=(0, (2.4, 1.6)),
                                connectionstyle="arc3,rad=0"), zorder=2)
    ax.plot([ora.left[0] - 0.16, ora.left[0]], [ora.left[1], ora.left[1]],
            color=cs.INK_MUTED, lw=0.8, ls=(0, (2.4, 1.6)), zorder=2)
    ax.plot([pol.left[0] - 0.16, pol.left[0]], [pol.left[1], pol.left[1]],
            color=cs.INK_MUTED, lw=0.8, ls=(0, (2.4, 1.6)), zorder=2)
    ax.text(pol.left[0] - 0.22, (pol.left[1] + ora.left[1]) / 2, "progress",
            fontsize=5.9, color=cs.INK_MUTED, ha="right", va="center",
            rotation=90)
    ax.set_title("(c)  host loop", loc="left", fontsize=7.4, pad=2)
    return c


def build(outdir):
    cs.use_paper_style()
    fig = plt.figure(figsize=(7.00, 2.78))
    fig.subplots_adjust(left=0.012, right=0.992, bottom=0.155, top=0.90,
                        wspace=0.10)
    gs = fig.add_gridspec(1, 3, width_ratios=[1.28, 0.78, 1.18])
    a = fig.add_subplot(gs[0, 0])
    b = fig.add_subplot(gs[0, 1])
    c = fig.add_subplot(gs[0, 2])
    panel_a(a)
    panel_b(b).check(pad=0.02)
    panel_c(c).check(pad=0.02)

    fig.add_artist(FancyBboxPatch(
        (0.01, 0.012), 0.98, 0.115, transform=fig.transFigure,
        boxstyle="round,pad=0,rounding_size=0.008",
        facecolor="#F4F4F4", edgecolor=cs.GRID, linewidth=0.6, clip_on=False))
    fig.text(0.03, 0.070,
             "compiled out:  0 netlist lines     ·     compiled in, mask = 0:  "
             "SMP  8/8     ·     ISA  84/84     ·     benches  5/5",
             fontsize=6.6, color=cs.INK, va="center")

    os.makedirs(outdir, exist_ok=True)
    print(cs.save(fig, os.path.join(outdir, "fig_mechanism")))


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default=os.path.join(ROOT, "paper/dac27/figures"))
    build(ap.parse_args().outdir)
