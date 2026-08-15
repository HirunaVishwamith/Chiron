#!/usr/bin/env python3
"""
profile_visualize.py — render the chiron profiling report.

    python3 scripts/profile_visualize.py <results_dir> [--out <output.png>]

Reads every JSON in <results_dir>. Two schemas are produced by the harnesses and
BOTH are understood here:

  single-core (sim/rtl/profiler.h)      {"benchmark", "raw": {...}, "derived": {...}}
  quad-core   (sim/rtl/profiler_quad.h) {"benchmark", "aggregate": {...}, "cores": [...]}

Reading only the single-core shape is what used to make every quad-core bar plot
as zero: `derived.ipc` does not exist in a quad record, so the lookup fell
through to its 0.0 default and the report claimed the four-core machine retired
no instructions at all.

Design notes, so the next person does not have to re-derive them:
  * Colour is assigned per BENCHMARK FAMILY and per CORE COUNT — never per rank —
    so adding or dropping a family never repaints the others.
  * Hues are taken in the documented slot order of a palette validated for
    colour-vision deficiency; they are not picked by eye.
  * No panel uses two y-axes. Where two measures share a panel they share units.
  * Every series is direct-labelled as well as legended, so identity never rests
    on colour alone, and the table at the bottom is the text view of the figure.
"""

import argparse
import glob
import json
import os
import re
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

# ── Palette ──────────────────────────────────────────────────────────────────
# Categorical slots 1..5, in the order the palette documents as validated for
# adjacent-pair separation under colour-vision deficiency.
FAMILY_COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"]
CORE_COLORS = {1: "#2a78d6", 4: "#eb6834"}

INK = "#0b0b0b"
INK_2 = "#52514e"
INK_MUTED = "#8a8983"
SURFACE = "#fcfcfb"
GRID = "#e4e3de"


# ── Loading ──────────────────────────────────────────────────────────────────

def parse_name(name):
    """'filter-s5-q4' -> ('filter', 5, 4);  'filter-s5' -> ('filter', 5, 1)."""
    cores = 4 if name.endswith("-q4") else 1
    stem = name[:-3] if cores == 4 else name
    m = re.match(r"^(.*)-s(\d+)$", stem)
    if m:
        return m.group(1), int(m.group(2)), cores
    return stem, 1, cores


def weighted(cores, value_key, weight_key):
    """Weighted mean of a per-core derived rate (a plain mean over-weights an
    idle hart, which is exactly the hart whose rate is least meaningful)."""
    num = den = 0.0
    for c in cores:
        w = c.get("raw", {}).get(weight_key, 0) or 0
        num += w * c.get("derived", {}).get(value_key, 0.0)
        den += w
    return num / den if den else 0.0


def load(results_dir):
    files = sorted(glob.glob(os.path.join(results_dir, "*.json")))
    if not files:
        print(f"[profile_visualize] no JSON in {results_dir}", file=sys.stderr)
        sys.exit(1)

    out = []
    for path in files:
        try:
            with open(path) as fh:
                d = json.load(fh)
        except Exception as exc:
            print(f"[profile_visualize] skipping {path}: {exc}", file=sys.stderr)
            continue

        name = d.get("benchmark") or os.path.basename(path)[:-5]
        family, scale, ncores = parse_name(name)

        if "aggregate" in d:                      # quad-core schema
            agg = d["aggregate"]
            cores = d.get("cores", [])
            rec = dict(
                ipc=agg.get("aggregate_ipc", 0.0),
                cycles=agg.get("max_cycles", 0),
                inst=agg.get("total_inst_retired", 0),
                branch=weighted(cores, "branch_accuracy_pct", "branch_total"),
                dmiss=weighted(cores, "dcache_miss_rate_pct", "dcache_reqs"),
                imiss=weighted(cores, "icache_miss_rate_pct", "inst_retired"),
                rob=weighted(cores, "rob_stall_pct", "cycles"),
            )
        else:                                     # single-core schema
            raw, drv = d.get("raw", {}), d.get("derived", {})
            rec = dict(
                ipc=drv.get("ipc", 0.0),
                cycles=raw.get("cycles", 0),
                inst=raw.get("inst_retired", 0),
                branch=drv.get("branch_accuracy_pct", 0.0),
                dmiss=drv.get("dcache_miss_rate_pct", 0.0),
                imiss=drv.get("icache_miss_rate_pct", 0.0),
                rob=drv.get("rob_stall_pct", 0.0),
            )

        rec.update(name=name, family=family, scale=scale, ncores=ncores)
        out.append(rec)
    return out


# ── Text view ────────────────────────────────────────────────────────────────

def print_table(recs):
    cols = ["Benchmark", "Cores", "Cycles", "Instr", "IPC",
            "Branch%", "I$ miss%", "D$ miss%", "ROB stall%"]
    rows = [[r["name"], str(r["ncores"]), f"{r['cycles']:,}", f"{r['inst']:,}",
             f"{r['ipc']:.3f}", f"{r['branch']:.1f}", f"{r['imiss']:.2f}",
             f"{r['dmiss']:.2f}", f"{r['rob']:.1f}"]
            for r in sorted(recs, key=lambda r: (r["family"], r["scale"], r["ncores"]))]
    w = [max(len(c), *(len(r[i]) for r in rows)) for i, c in enumerate(cols)]
    sep = "+-" + "-+-".join("-" * x for x in w) + "-+"
    print(sep)
    print("| " + " | ".join(c.ljust(w[i]) for i, c in enumerate(cols)) + " |")
    print(sep)
    for r in rows:
        print("| " + " | ".join(r[i].ljust(w[i]) for i in range(len(cols))) + " |")
    print(sep)


# ── Figure ───────────────────────────────────────────────────────────────────

def style_axes(ax, ylabel=None, xlabel=None):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK_2, labelsize=8.5, length=3, width=0.8)
    ax.grid(True, axis="y", color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    if ylabel:
        ax.set_ylabel(ylabel, color=INK_2, fontsize=9)
    if xlabel:
        ax.set_xlabel(xlabel, color=INK_2, fontsize=9)


def stat_tile(ax, value, label, sub):
    ax.axis("off")
    ax.text(0, 0.62, value, fontsize=26, fontweight="bold", color=INK,
            ha="left", va="center")
    ax.text(0, 0.24, label, fontsize=10, color=INK_2, ha="left", va="center")
    ax.text(0, 0.04, sub, fontsize=8.5, color=INK_MUTED, ha="left", va="center")


def render(recs, out_path):
    families = sorted({r["family"] for r in recs})
    fam_color = {f: FAMILY_COLORS[i % len(FAMILY_COLORS)] for i, f in enumerate(families)}
    by = {(r["family"], r["scale"], r["ncores"]): r for r in recs}

    fig = plt.figure(figsize=(16.5, 12.4), facecolor=SURFACE)
    gs = fig.add_gridspec(
        4, len(families),
        height_ratios=[0.52, 1.35, 1.15, 0.05],
        hspace=0.62, wspace=0.28,
        left=0.055, right=0.985, top=0.915, bottom=0.055,
    )

    fig.text(0.055, 0.965, "chiron — quad-core RV64IMA out-of-order processor",
             fontsize=19, fontweight="bold", color=INK, ha="left")
    fig.text(0.055, 0.938,
             "Cycle-accurate RTL profile · every benchmark family at every scale "
             "it has a dataset for · 1 core vs 4 cores",
             fontsize=10.5, color=INK_2, ha="left")

    # ── Row 1: headline numbers ──────────────────────────────────────────────
    quad = [r for r in recs if r["ncores"] == 4]
    single = [r for r in recs if r["ncores"] == 1]
    best_q = max(quad, key=lambda r: r["ipc"]) if quad else None

    speedups = []
    for r in quad:
        s = by.get((r["family"], r["scale"], 1))
        if s and r["cycles"]:
            speedups.append((s["cycles"] / r["cycles"], r))
    best_sp = max(speedups, key=lambda t: t[0]) if speedups else None

    tiles = []
    if best_q:
        tiles.append((f"{best_q['ipc']:.2f}", "peak aggregate IPC (4 cores)",
                      f"{best_q['family']}-s{best_q['scale']}"))
    if single:
        b = max(single, key=lambda r: r["ipc"])
        tiles.append((f"{b['ipc']:.2f}", "peak IPC (1 core)",
                      f"{b['family']}-s{b['scale']}"))
    if best_sp:
        tiles.append((f"{best_sp[0]:.2f}×", "best 4-core speed-up",
                      f"{best_sp[1]['family']}-s{best_sp[1]['scale']}"))
    if quad:
        tiles.append((f"{sum(r['branch'] for r in quad)/len(quad):.1f}%",
                      "mean branch accuracy", "TAGE + BTB + RAS, 4 cores"))
    tiles.append((str(len(recs)), "profiled configurations",
                  f"{len(families)} families · 1 and 4 cores"))

    for i, (v, lab, sub) in enumerate(tiles[:len(families)]):
        stat_tile(fig.add_subplot(gs[0, i]), v, lab, sub)

    # ── Row 2: IPC vs scale, one small multiple per family ───────────────────
    # Small multiples rather than one 46-bar chart: the question is "how does
    # this family scale", and that reads per family, not across families.
    ipc_max = max([r["ipc"] for r in recs] + [1.0]) * 1.18
    for i, fam in enumerate(families):
        ax = fig.add_subplot(gs[1, i])
        style_axes(ax, ylabel="IPC" if i == 0 else None)
        scales = sorted({r["scale"] for r in recs if r["family"] == fam})
        x = np.arange(len(scales))
        width = 0.38
        for k, nc in enumerate((1, 4)):
            vals = [by.get((fam, s, nc), {}).get("ipc", np.nan) for s in scales]
            bars = ax.bar(x + (k - 0.5) * (width + 0.03), vals, width,
                          color=CORE_COLORS[nc], zorder=3,
                          label=f"{nc} core" + ("s" if nc > 1 else ""))
            for b, v in zip(bars, vals):
                if v and not np.isnan(v):
                    ax.text(b.get_x() + b.get_width() / 2, v + ipc_max * 0.02,
                            f"{v:.2f}", ha="center", va="bottom",
                            fontsize=7.4, color=INK_2)
                else:
                    # A configuration that produced no result is called out
                    # rather than left as blank space, so a gap in the data is
                    # never mistaken for a measurement of zero -- which is the
                    # exact failure this script had.
                    ax.text(b.get_x() + b.get_width() / 2, ipc_max * 0.02,
                            "n/a", ha="center", va="bottom", rotation=90,
                            fontsize=6.8, color=INK_MUTED)
        ax.set_xticks(x)
        ax.set_xticklabels([f"s{s}" for s in scales])
        ax.set_ylim(0, ipc_max)
        ax.set_title(fam, fontsize=11, fontweight="bold", color=INK, pad=8)
        if i == 0:
            first_ipc_ax = ax
        handles, labels = ax.get_legend_handles_labels()

    # One shared legend and one caption for the whole row, positioned from the
    # axes' real geometry -- a hardcoded figure y drifts the moment the row
    # count or figure size changes, and lands on top of the bars.
    box = first_ipc_ax.get_position()
    fig.legend(handles, labels, frameon=False, fontsize=9,
               loc="lower left", bbox_to_anchor=(box.x0, box.y1 + 0.035),
               ncol=2, labelcolor=INK_2)
    fig.text(box.x0, box.y0 - 0.045,
             "Instructions per cycle — higher is better. 4-core bars are the "
             "aggregate across all four harts.",
             fontsize=9.5, color=INK_2, ha="left", va="top")

    # ── Row 3 left: 4-core speed-up vs scale ─────────────────────────────────
    half = max(2, len(families) // 2)
    ax = fig.add_subplot(gs[2, :half])
    style_axes(ax, ylabel="speed-up (×)", xlabel="dataset scale")
    for fam in families:
        scales = sorted({r["scale"] for r in recs if r["family"] == fam})
        xs, ys = [], []
        for s in scales:
            q, sc = by.get((fam, s, 4)), by.get((fam, s, 1))
            if q and sc and q["cycles"]:
                xs.append(s)
                ys.append(sc["cycles"] / q["cycles"])
        if xs:
            ax.plot(xs, ys, marker="o", markersize=5, linewidth=2,
                    color=fam_color[fam], label=fam, zorder=3)
            ax.annotate(fam, (xs[-1], ys[-1]), textcoords="offset points",
                        xytext=(7, 0), fontsize=8.5, color=fam_color[fam],
                        va="center", fontweight="bold")
    ax.axhline(1.0, color=INK_MUTED, linewidth=1, linestyle=(0, (4, 3)), zorder=2)
    ax.text(ax.get_xlim()[0], 1.0, " no gain", fontsize=8, color=INK_MUTED,
            va="bottom", ha="left")
    ax.set_xticks(sorted({r["scale"] for r in recs}))
    ax.set_xticklabels([f"s{s}" for s in sorted({r["scale"] for r in recs})])
    ax.set_title("Cycles saved by running on four cores",
                 fontsize=11, fontweight="bold", color=INK, pad=8)
    ax.legend(frameon=False, fontsize=8.5, ncol=2, loc="upper left",
              labelcolor=INK_2)

    # ── Row 3 right: where the cycles go (4 cores, largest scale) ────────────
    ax = fig.add_subplot(gs[2, half:])
    style_axes(ax, ylabel="percent")
    largest = []
    for fam in families:
        cand = [r for r in quad if r["family"] == fam]
        if cand:
            largest.append(max(cand, key=lambda r: r["scale"]))
    x = np.arange(len(largest))
    width = 0.26
    series = [("branch accuracy", "branch", "#1baf7a"),
              ("ROB-head stall", "rob", "#eda100"),
              ("D$ miss rate", "dmiss", "#e34948")]
    for k, (label, key, color) in enumerate(series):
        vals = [r[key] for r in largest]
        bars = ax.bar(x + (k - 1) * (width + 0.02), vals, width, color=color,
                      zorder=3, label=label)
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v + 1.5, f"{v:.0f}",
                    ha="center", va="bottom", fontsize=7.4, color=INK_2)
    ax.set_xticks(x)
    ax.set_xticklabels([f"{r['family']}\ns{r['scale']}" for r in largest],
                       fontsize=8.5)
    ax.set_ylim(0, 108)
    ax.set_title("Behaviour at each family's largest scale (4 cores)",
                 fontsize=11, fontweight="bold", color=INK, pad=8)
    ax.legend(frameon=False, fontsize=8.5, ncol=3, loc="upper left",
              labelcolor=INK_2)

    box3 = ax.get_position()
    fig.text(0.055, box3.y0 - 0.055,
             "All three are percentages, so they share one axis. Branch accuracy "
             "is higher-is-better; the other two are lower-is-better. "
             "Full per-configuration numbers are printed as a table alongside this figure.",
             fontsize=8.5, color=INK_MUTED, ha="left", va="top")

    fig.savefig(out_path, dpi=150, facecolor=SURFACE, bbox_inches="tight")
    print(f"[profile_visualize] wrote {out_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir")
    ap.add_argument("--out", default="docs/profile_report.png")
    args = ap.parse_args()

    recs = load(args.results_dir)
    print_table(recs)
    if not HAS_MATPLOTLIB:
        print("[profile_visualize] matplotlib not installed; table only",
              file=sys.stderr)
        return
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    render(recs, args.out)


if __name__ == "__main__":
    main()
