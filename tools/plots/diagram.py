"""Primitives for hand-drawn-quality architectural diagrams in matplotlib.

Why matplotlib and not Inkscape/TikZ: the diagrams in this paper have to stay
consistent with the plots (same palette, same type, same line weights) and have
to be regenerable when the design changes. A diagram that drifts out of date
because it lives in a binary file is worse than no diagram.

Everything here works in *axes coordinates* on a unit-ish grid so a diagram can
be composed by naming positions rather than by nudging numbers.
"""

import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
from matplotlib.path import Path

import chiron_style as cs

# Semantic fills, not decorative ones. A reader should be able to tell what
# KIND of thing a box is without reading it.
FILL = {
    "rtl":      "#EAF2F8",   # existing hardware, untouched
    "hook":     "#FDE9DC",   # the one thing Kairos adds to the design
    "sw":       "#F2F2F2",   # host-side software
    "kairos":   "#E8F3EE",   # Kairos components
    "artifact": "#FFF8E1",   # outputs a human reads
}
EDGE = {
    "rtl": "#5B7B95", "hook": "#D55E00", "sw": "#8A8A8A",
    "kairos": "#009E73", "artifact": "#B8860B",
}


def box(ax, x, y, w, h, label, kind="rtl", sub=None, fontsize=7,
        radius=0.02, lw=0.9, zorder=2):
    """A rounded box with a title and an optional second line."""
    p = mpatches.FancyBboxPatch(
        (x, y), w, h,
        boxstyle=mpatches.BoxStyle("Round", pad=0, rounding_size=radius),
        linewidth=lw, edgecolor=EDGE[kind], facecolor=FILL[kind], zorder=zorder)
    ax.add_patch(p)
    if sub:
        ax.text(x + w / 2, y + h * 0.60, label, ha="center", va="center",
                fontsize=fontsize, color=cs.INK, zorder=zorder + 1)
        ax.text(x + w / 2, y + h * 0.26, sub, ha="center", va="center",
                fontsize=fontsize - 1.3, color=cs.INK_MUTED, zorder=zorder + 1)
    else:
        ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
                fontsize=fontsize, color=cs.INK, zorder=zorder + 1)
    return (x, y, w, h)


def arrow(ax, p0, p1, label=None, kind="rtl", style="-|>", rad=0.0,
          fontsize=6, lw=0.9, ls="-", label_offset=(0, 0.018), zorder=3):
    """A connector. `rad` bends it, for routing around other boxes."""
    ax.annotate("", xy=p1, xytext=p0,
                arrowprops=dict(arrowstyle=style, color=EDGE[kind], lw=lw,
                                linestyle=ls, shrinkA=1.5, shrinkB=1.5,
                                connectionstyle=f"arc3,rad={rad}"),
                zorder=zorder)
    if label:
        mx, my = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
        ax.text(mx + label_offset[0], my + label_offset[1], label,
                ha="center", va="bottom", fontsize=fontsize,
                color=EDGE[kind], zorder=zorder + 1)


def and_gate(ax, cx, cy, r=0.016, color=None, zorder=4):
    """A small AND body — the actual mechanism, so it is drawn, not described."""
    color = color or EDGE["hook"]
    verts = [(cx - r, cy - r), (cx, cy - r), (cx + r, cy), (cx, cy + r),
             (cx - r, cy + r), (cx - r, cy - r)]
    codes = [Path.MOVETO] + [Path.LINETO] * 4 + [Path.CLOSEPOLY]
    ax.add_patch(mpatches.PathPatch(Path(verts, codes), facecolor="white",
                                    edgecolor=color, lw=1.0, zorder=zorder))
    ax.text(cx - r * 0.15, cy, "&", ha="center", va="center", fontsize=6,
            color=color, zorder=zorder + 1)


def bubble(ax, x, y, r=0.007, color=None, zorder=5):
    """An inversion bubble."""
    color = color or EDGE["hook"]
    ax.add_patch(mpatches.Circle((x, y), r, facecolor="white",
                                 edgecolor=color, lw=0.9, zorder=zorder))


def group(ax, x, y, w, h, label, color=None, ls=(0, (3, 2)), zorder=1,
          label_pos="tl", fontsize=6.5):
    """A dashed grouping frame with a corner label."""
    color = color or cs.INK_MUTED
    ax.add_patch(mpatches.FancyBboxPatch(
        (x, y), w, h,
        boxstyle=mpatches.BoxStyle("Round", pad=0, rounding_size=0.012),
        linewidth=0.7, edgecolor=color, facecolor="none", linestyle=ls,
        zorder=zorder))
    if label_pos == "tl":
        ax.text(x + 0.008, y + h - 0.012, label, ha="left", va="top",
                fontsize=fontsize, color=color, style="italic", zorder=zorder)
    else:
        ax.text(x + w - 0.008, y + 0.010, label, ha="right", va="bottom",
                fontsize=fontsize, color=color, style="italic", zorder=zorder)


def note(ax, x, y, text=None, ha="left", va="bottom", fontsize=6.2,
         color=None, weight="normal"):
    """A text annotation. Pass va="top" for multi-line prose so the block hangs
    from `y` — a bottom-anchored block grows upward into whatever is above it,
    which is how diagram text ends up on top of boxes."""
    ax.text(x, y, text, ha=ha, va=va, fontsize=fontsize,
            color=color or cs.INK_MUTED, weight=weight, zorder=6)


def blank_axes(ax):
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.set_axis_off()
    ax.grid(False)
    return ax
