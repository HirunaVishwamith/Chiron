#!/usr/bin/env python3
"""Build every Kairos figure for the manuscript, from measured data only.

    tools/kairos/figures.py
    tools/kairos/figures.py --outdir DIR

In the manuscript (main.tex), in order:

  fig1_teaser       Fig 1  the claim: 1 vs. many interleavings, plus the
                           reduced schedule. PCT is drawn hollow/dashed when
                           every one of its runs hit the cycle budget.
  fig_compare       Fig 2  capability matrix; cells must agree with Table 1
  fig_mechanism     Fig 3  the RTL change, the DUT, the host loop
  fig_exploration   Fig 4  fingerprint matrix + order-pair coverage. A run
                           that timed out is hatched: its digest is of a
                           truncated execution, and saying so is the
                           difference between a comparison and a claim.
  fig_finding       Fig 5  one finding end to end: the delay, and the reduction

Built but not currently included — kept as alternates:

  fig2_architecture the full Kairos stack, incl. the finding pipeline
  fig3_hook         the RTL change on its own
  fig_determinism   reverting a real coherence bug changes nothing observable
  fig_tte           time-to-exposure
  fig6_position     two orthogonal axes (program vs. schedule × observation)
  table_findings    every finding with its reproduction command
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PLOTS = os.path.join(HERE, "..", "plots")


def run(script, *args, cwd=None):
    path = script if os.path.isabs(script) else os.path.join(HERE, script)
    cmd = [sys.executable, path, *args]
    print("  " + os.path.basename(path) + " " + " ".join(args[:2]))
    subprocess.run(cmd, check=True, cwd=cwd)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reports", default="build/kairos/mt-*.json")
    ap.add_argument("--outdir", default="paper/dac27/figures")
    a = ap.parse_args()

    reports = sorted(g for g in glob.glob(a.reports)
                     if not g.endswith("shrink-trace.json")
                     and "fixed" not in os.path.basename(g)
                     and "spinwait-fixed" not in g)
    if not reports:
        sys.exit(f"no campaign reports matched {a.reports} — run `make kairos-sweep`")

    os.makedirs(a.outdir, exist_ok=True)

    run("fig1_teaser.py")
    run("fig2_architecture.py")
    run("fig3_hook.py", "--outdir", a.outdir)
    run("fig_mechanism.py", "--outdir", a.outdir)
    run("fig_compare.py")
    run("fig_tte.py")
    run(os.path.join(PLOTS, "fig_determinism.py"))
    run("fig_exploration.py", *reports, "--outdir", a.outdir)
    run("fig_finding.py", "--outdir", a.outdir)
    run("fig6_position.py")
    run("analyze.py", *reports, "--outdir", a.outdir, "--table-only")
    print(f"figures in {a.outdir}")


if __name__ == "__main__":
    main()
