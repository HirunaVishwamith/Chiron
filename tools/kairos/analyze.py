#!/usr/bin/env python3
"""Turn Kairos campaign reports into the figures and tables for the paper.

Reads the JSON written by `kairos --json` (one file per workload, as produced
by `make kairos-sweep`) and emits:

    fig_exploration.pdf   distinct interleavings vs. runs, per policy
    fig_novelty.pdf       novelty rate per policy, per workload
    fig_shrink.pdf        schedule size before/after reduction
    table_findings.tex    every finding, with its reproduction command

Usage:
    tools/kairos/analyze.py build/kairos/*.json [--outdir paper/dac27/figures]

The plotting style is shared with every other figure in this project
(tools/plots/chiron_style.py): one palette, one figure size vocabulary, Type 42
fonts, 600 dpi.
"""

import argparse
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "plots"))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import chiron_style as cs

# The order arms are drawn in, and therefore the order they are coloured in.
# Fixed on purpose: a policy must keep its colour when a workload has fewer
# arms, or a reader comparing two figures reads the wrong series.
POLICY_ORDER = ["deterministic", "random", "pct", "windowed"]

# Two policies that explore equally well trace the SAME curve, and the one drawn
# second erases the first -- a reader then sees three series where there are
# four. Colour alone cannot fix that, so each policy also owns a dash pattern.
POLICY_DASH = {
    "deterministic": (None, None),
    "random": (5, 1.6),
    "pct": (1.6, 1.6),
    "windowed": (None, None),
}


def policy_family(spec):
    """'random:0.02' -> 'random'.  The parameter lives in the legend label."""
    return spec.split(":", 1)[0]


def policy_color(spec):
    """Colour follows the POLICY, never its rank in a particular figure.

    A workload that happens to have fewer arms must not repaint the survivors,
    or a reader comparing two panels reads the wrong series.
    """
    return {
        "deterministic": cs.BASELINE,
        "random": cs.RANDOM,
        "pct": cs.OURS,
        "windowed": cs.BRUTE,
    }.get(policy_family(spec), cs.CATEGORICAL[4])


def load(paths):
    """Return {workload: report}. The workload name is the file stem."""
    out = {}
    for p in paths:
        with open(p) as f:
            out[os.path.splitext(os.path.basename(p))[0]] = json.load(f)
    return out


def sort_policies(specs):
    return sorted(specs, key=lambda s: (POLICY_ORDER.index(policy_family(s))
                                        if policy_family(s) in POLICY_ORDER
                                        else len(POLICY_ORDER), s))


# ── Figure 1: exploration curve ─────────────────────────────────────────────
# Distinct interleavings discovered as a function of runs spent. The
# deterministic arm is the flat line at 1 -- that flat line IS the argument, so
# it is drawn even though it carries no information of its own.
def fig_exploration(reports, outdir):
    fig, axes = plt.subplots(
        1, len(reports), figsize=cs.figsize("double"), sharey=True)
    if len(reports) == 1:
        axes = [axes]

    for ax, (name, rep) in zip(axes, sorted(reports.items())):
        by_policy = defaultdict(list)
        for r in rep["runs"]:
            by_policy[r["policy"]].append(r)

        for spec in sort_policies(by_policy):
            seen, curve = set(), []
            for r in by_policy[spec]:
                seen.add(r["digest"])
                curve.append(len(seen))
            dash = POLICY_DASH.get(policy_family(spec), (None, None))
            ax.plot(range(1, len(curve) + 1), curve, lw=2,
                    color=policy_color(spec), label=spec,
                    dashes=dash if dash[0] else (),
                    solid_capstyle="round")

        ax.set_title(name, fontsize=9)
        ax.set_xlabel("schedules run")
        ax.grid(alpha=0.25, lw=0.6)
    axes[0].set_ylabel("distinct interleavings")
    axes[-1].legend(frameon=False, fontsize=7, handlelength=1.4,
                    borderaxespad=0.3)
    fig.tight_layout()
    print(cs.save(fig, os.path.join(outdir, "fig_exploration")))


# ── Figure 2: novelty rate ──────────────────────────────────────────────────
def fig_novelty(reports, outdir):
    workloads = sorted(reports)
    specs = sort_policies({s for r in reports.values() for s in r["policies"]})

    fig, ax = plt.subplots(figsize=cs.figsize("single"))
    width = 0.8 / max(len(specs), 1)
    for i, spec in enumerate(specs):
        xs, ys = [], []
        for j, w in enumerate(workloads):
            pol = reports[w]["policies"].get(spec)
            if pol is None:
                continue
            xs.append(j + i * width - 0.4 + width / 2)
            ys.append(pol["novelty_rate"])
        ax.bar(xs, ys, width * 0.9, color=policy_color(spec), label=spec)

    ax.set_xticks(range(len(workloads)))
    ax.set_xticklabels(workloads, rotation=30, ha="right", fontsize=7)
    ax.set_ylabel("fraction of runs yielding a new interleaving")
    ax.set_ylim(0, 1.05)
    ax.grid(axis="y", alpha=0.25, lw=0.6)
    ax.legend(frameon=False, fontsize=7, handlelength=1.4, borderaxespad=0.3)
    fig.tight_layout()
    print(cs.save(fig, os.path.join(outdir, "fig_novelty")))


# ── Figure 3: reduction ─────────────────────────────────────────────────────
# Log scale, because the whole point is that the reduction is orders of
# magnitude. note_log_axis() puts that in the figure rather than the caption.
def fig_shrink(reports, outdir):
    before, after, labels = [], [], []
    for name, rep in sorted(reports.items()):
        for r in rep["runs"]:
            sh = r.get("shrink")
            if not sh:
                continue
            before.append(max(sh["cycles_before"], 1))
            after.append(max(sh["cycles_after"], 1))
            labels.append(f"{name}\nseed {r['seed']}")

    if not before:
        print("analyze: no shrink records — run with --shrink", file=sys.stderr)
        return

    fig, ax = plt.subplots(figsize=cs.figsize("single"))
    x = range(len(before))
    ax.bar([i - 0.2 for i in x], before, 0.4,
           color=cs.BASELINE, label="as found")
    ax.bar([i + 0.2 for i in x], after, 0.4,
           color=cs.OURS, label="after reduction")
    ax.set_yscale("log")
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, fontsize=6)
    ax.set_ylabel("hart-cycles held")
    ax.grid(axis="y", alpha=0.25, lw=0.6)
    ax.legend(frameon=False, fontsize=7, handlelength=1.4, borderaxespad=0.3)
    cs.note_log_axis(ax)
    fig.tight_layout()
    print(cs.save(fig, os.path.join(outdir, "fig_shrink")))


# ── Table: the findings ─────────────────────────────────────────────────────
# Every row carries its reproduction command. A findings table without one is
# an assertion; with one it is an artefact.
def table_findings(reports, outdir):
    rows = []
    for name, rep in sorted(reports.items()):
        for r in rep["runs"]:
            if r["verdict"] in ("ok", "timeout"):
                continue
            sh = r.get("shrink", {})
            rows.append((name, r["policy"], r["seed"], r["verdict"],
                         r["find_cycle"], sh.get("spans_after", "--"),
                         sh.get("cycles_after", "--")))

    path = os.path.join(outdir, "table_findings.tex")
    with open(path, "w") as f:
        f.write("% generated by tools/kairos/analyze.py -- do not edit\n")
        f.write("\\begin{tabular}{llrlrrr}\n\\toprule\n")
        f.write("Workload & Policy & Seed & Oracle & Cycle & Spans & "
                "Hart-cyc.\\\\\n\\midrule\n")
        for r in rows:
            f.write(" & ".join(str(c).replace("_", "\\_") for c in r) + "\\\\\n")
        if not rows:
            f.write("\\multicolumn{7}{c}{no findings in this campaign}\\\\\n")
        f.write("\\bottomrule\n\\end{tabular}\n")
    print(f"wrote {path} ({len(rows)} finding(s))")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("reports", nargs="+", help="kairos --json output files")
    ap.add_argument("--outdir", default="paper/dac27/figures")
    ap.add_argument("--table-only", action="store_true",
                    help="emit only table_findings.tex (the plots now live in "
                         "fig_exploration.py / fig_finding.py, which say more "
                         "than the bar charts this script used to draw)")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    reports = load(args.reports)
    cs.use_paper_style()

    if not args.table_only:
        fig_exploration(reports, args.outdir)
        fig_novelty(reports, args.outdir)
        fig_shrink(reports, args.outdir)
    table_findings(reports, args.outdir)


if __name__ == "__main__":
    main()
