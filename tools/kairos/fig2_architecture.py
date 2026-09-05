#!/usr/bin/env python3
"""Architecture: left = Kairos, right = DUT, bottom = finding pipeline.

No wrapping arrows. Every connector is between neighbouring boxes.
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib.pyplot as plt

from chiron_style import CATEGORICAL, save, use_paper_style, figsize
from schematic import Canvas

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
BLUE, ORANGE = CATEGORICAL[0], CATEGORICAL[1]


def build(ax):
    c = Canvas(ax, 16.0, 10.2)

    # ── left: Kairos ───────────────────────────────────────────────────────
    c.group(0.15, 3.35, 6.55, 6.55, "KAIROS  (host)", BLUE)

    pol = c.box(0.45, 8.15, 5.95, title="Policy",
                body="deterministic  ·  random  ·  PCT  ·  windowed",
                role="ours")
    mask = c.box(0.45, 6.35, 5.95, title="stall mask",
                body="1 bit per hart   ·   delay only",
                role="ours")
    ora = c.box(0.45, 4.55, 2.80, title="Oracle",
                body="result · hang\nlivelock · SWMR",
                role="ours")
    cov = c.box(3.50, 4.55, 2.90, title="Coverage",
                body="digest\norder pairs",
                role="ours")

    # No label here: the Policy->mask gutter is one arrow tall, so any
    # label lands inside the Policy box (schematic.check() enforces this).
    c.arrow(pol.bottom, mask.top)
    c.arrow(mask.port("bottom", 0.28), ora.top)
    c.arrow(mask.port("bottom", 0.72), cov.top)

    # ── right: DUT ─────────────────────────────────────────────────────────
    c.group(6.95, 3.35, 8.85, 6.55, "CHIRON  (DUT)", ORANGE)

    c0 = c.box(7.25, 8.25, 1.95, title="core 0", body="OoO L1", role="dut")
    c1 = c.box(9.40, 8.25, 1.95, title="core 1", body="OoO L1", role="dut")
    c2 = c.box(11.55, 8.25, 1.95, title="core 2", body="OoO L1", role="dut")
    c3 = c.box(13.70, 8.25, 1.75, title="core 3", body="OoO L1", role="dut")
    ccu = c.box(7.25, 6.35, 4.10, title="CCU", body="ACE snoops", role="dut")
    l2  = c.box(11.55, 6.35, 3.90, title="shared L2", body="MSHR · PLRU", role="dut")
    ev  = c.box(7.25, 4.55, 8.20, title="event stream  (read-only)",
                body="retire  ·  tag-state change   ·   nothing written back",
                role="dut")

    c.arrow(c0.bottom, ccu.port("top", 0.22))
    c.arrow(c1.bottom, ccu.port("top", 0.72))
    c.arrow(c2.bottom, l2.port("top", 0.30))
    c.arrow(c3.bottom, l2.port("top", 0.78))
    c.arrow(ccu.bottom, ev.port("top", 0.28))
    c.arrow(l2.bottom, ev.port("top", 0.72))

    # the only cross-group arrows: short, through the gap, no boxes in the way
    c.arrow(mask.right, c0.port("left", 0.5), label="delay", label_size=6.2)
    # Stay in the gap between the two groups, under Coverage, so the
    # observe arrow never crosses a box.
    c.arrow(ev.port("left", 0.12), ora.port("right", 0.18),
            waypoints=[(6.72, 3.82)],
            label="observe", label_size=6.2, label_side="below")

    # ── finding pipeline ───────────────────────────────────────────────────
    c.group(0.15, 0.15, 15.65, 2.90, "FINDING PIPELINE", ORANGE)
    v = c.box(0.40, 0.50, 3.20, title="Verdict",
              body="layer · cycle · hart", role="bad")
    r = c.box(4.00, 0.50, 3.40, title="Schedule record",
              body="[start, end)  mask", role="artifact")
    s = c.box(7.80, 0.50, 3.60, title="Reducer",
              body="ddmin + edge search", role="ours")
    p = c.box(11.80, 0.50, 3.65, title="Replay",
              body="bit-exact  ·  no seed", role="good")
    c.arrow(v.right, r.left)
    c.arrow(r.right, s.left)
    c.arrow(s.right, p.left)
    return c


def main():
    use_paper_style()
    fig, ax = plt.subplots(figsize=(7.00, 2.70))
    fig.subplots_adjust(left=0.01, right=0.99, bottom=0.02, top=0.98)
    build(ax).check()
    out = save(fig, os.path.join(ROOT, "paper/dac27/figures/fig2_architecture"))
    print("wrote", out)


if __name__ == "__main__":
    main()
