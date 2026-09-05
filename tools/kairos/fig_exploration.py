#!/usr/bin/env python3
"""Figure 4 — what a deterministic simulator actually explores.

Top: a fingerprint matrix, one cell per (policy, seed). Filled = an interleaving
not seen before in that row; hollow = a repeat of class n. The deterministic
row is one filled cell and seven hollow ones, on every workload.

Bottom: cross-hart order-pair coverage — a coarser metric that does not
flatter a policy that merely jitters timing.
"""
import argparse, json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

import chiron_style as cs

ORDER = ["deterministic", "random", "pct", "windowed"]
SHORT = {"deterministic": "deterministic",
         "random": "random", "pct": "PCT", "windowed": "windowed"}
COLOR = cs.POLICY_COLOR


def fam(spec):
    return spec.split(":", 1)[0]


def sort_specs(specs):
    return sorted(specs, key=lambda s: (ORDER.index(fam(s)) if fam(s) in ORDER
                                        else len(ORDER), s))


def load(paths):
    out = {}
    for p in paths:
        with open(p) as f:
            out[os.path.splitext(os.path.basename(p))[0]] = json.load(f)
    return out


def build(reports, outdir):
    cs.use_paper_style()
    names = sorted(reports)
    n = len(names)
    fig, axes = plt.subplots(2, n, figsize=cs.figsize("explore"),
                             gridspec_kw={"height_ratios": [2.05, 1.00]})
    fig.subplots_adjust(left=0.14, right=0.97, bottom=0.185, top=0.90,
                        hspace=0.42, wspace=0.22)
    if n == 1:
        axes = axes.reshape(2, 1)

    for col, name in enumerate(names):
        rep = reports[name]
        runs = rep["runs"]
        specs = sort_specs({r["policy"] for r in runs})
        nruns = max(len([r for r in runs if r["policy"] == s]) for s in specs)

        ax = axes[0][col]
        ax.grid(False)
        for row, spec in enumerate(specs):
            mine = [r for r in runs if r["policy"] == spec]
            classes = {}
            for i, r in enumerate(mine):
                d = r["digest"]
                fresh = d not in classes
                if fresh:
                    classes[d] = len(classes) + 1
                cls = classes[d]
                c = COLOR[fam(spec)]
                # A run that hit the cycle budget never finished the program,
                # so its digest is a digest of a TRUNCATED execution. It still
                # counts as a distinct schedule -- but the reader has to be
                # able to see it, or the policy comparison is not honest.
                cut = r.get("verdict") == "timeout"
                ax.add_patch(Rectangle(
                    (i + 0.08, row + 0.12), 0.84, 0.76,
                    facecolor=c if fresh else "white",
                    edgecolor=c, linewidth=0.9, alpha=1.0 if fresh else 0.70))
                if cut:
                    ax.add_patch(Rectangle(
                        (i + 0.08, row + 0.12), 0.84, 0.76,
                        facecolor="none", edgecolor="white", linewidth=0.0,
                        hatch="////", zorder=2))
                ax.text(i + 0.5, row + 0.5, str(cls), ha="center", va="center",
                        fontsize=6.6, color="white" if fresh else c,
                        weight="bold" if fresh else "normal", zorder=3)
            ax.text(nruns + 0.22, row + 0.5, f"{len(classes)}", ha="left",
                    va="center", fontsize=8.0, color=COLOR[fam(spec)],
                    weight="bold")

        cut_rows = {r["policy"] for r in runs if r.get("verdict") == "timeout"}

        ax.set_xlim(0, nruns + 0.95)
        ax.set_ylim(len(specs) - 0.02, -0.15)
        ax.set_yticks([i + 0.5 for i in range(len(specs))])
        ax.set_yticklabels(
            [SHORT[fam(s)] + ("$^\\dagger$" if s in cut_rows else "")
             for s in specs] if col == 0 else [""] * len(specs), fontsize=7.0)
        ax.set_xticks([i + 0.5 for i in range(nruns)])
        ax.set_xticklabels([str(i + 1) for i in range(nruns)], fontsize=6.4)
        ax.set_title(name, fontsize=8.0, pad=4)
        for s in ("top", "right", "left", "bottom"):
            ax.spines[s].set_visible(False)
        ax.tick_params(length=0)
        if col == 0:
            ax.set_xlabel("seed", fontsize=7.0, labelpad=2)

        bx = axes[1][col]
        vals = [rep["policies"][s]["distinct_order_pairs"] for s in specs]
        bars = bx.bar(range(len(specs)), vals,
                      color=[COLOR[fam(s)] for s in specs], width=0.62,
                      edgecolor="none")
        cs.label_bars(bx, bars, fmt="{:.0f}", offset=1.06)
        bx.set_xticks(range(len(specs)))
        bx.set_xticklabels([SHORT[fam(s)] for s in specs], fontsize=6.6)
        ymax = max(vals) if vals else 1
        bx.set_ylim(0, ymax * 1.38)
        if col == 0:
            bx.set_ylabel("distinct order pairs", fontsize=7.0)
        bx.tick_params(axis="y", labelsize=6.2)
        bx.grid(axis="x", visible=False)

    fig.text(0.14, 0.017,
             "white hatching / $\\dagger$: the run hit the cycle budget and never "
             "finished the program \u2014 its digest is of a truncated execution",
             fontsize=6.0, color=cs.INK_MUTED, ha="left", va="bottom")
    os.makedirs(outdir, exist_ok=True)
    print(cs.save(fig, os.path.join(outdir, "fig_exploration")))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("reports", nargs="+")
    ap.add_argument("--outdir", default="paper/dac27/figures")
    a = ap.parse_args()
    build(load(a.reports), a.outdir)
