#!/usr/bin/env python3
"""
profile_visualize.py — Visualize profiling JSON results for the RISCV-OOO processor.

Usage:
    python3 scripts/profile_visualize.py <results_dir> [--out <output.png>]

Each JSON file in <results_dir> must match the format produced by profiler.h.
"""

import argparse
import json
import os
import sys
import glob

# Try importing matplotlib; provide a helpful message if absent.
try:
    import matplotlib
    matplotlib.use("Agg")  # non-interactive backend
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


# ── JSON loading ─────────────────────────────────────────────────────────────

def load_results(results_dir: str) -> list:
    """Load all JSON files from results_dir, sorted by benchmark name."""
    pattern = os.path.join(results_dir, "*.json")
    files = sorted(glob.glob(pattern))

    if not files:
        print(f"[profile_visualize] No JSON files found in: {results_dir}", file=sys.stderr)
        sys.exit(1)

    records = []
    for f in files:
        try:
            with open(f) as fh:
                data = json.load(fh)
            records.append(data)
        except Exception as e:
            print(f"[profile_visualize] WARNING: skipping {f}: {e}", file=sys.stderr)

    return records


def get(record: dict, *keys, default=0.0):
    """Safely extract a nested value from a dict."""
    d = record
    for k in keys:
        if not isinstance(d, dict) or k not in d:
            return default
        d = d[k]
    return d if d is not None else default


# ── ASCII table ───────────────────────────────────────────────────────────────

def print_ascii_table(records: list):
    col_names = [
        "Benchmark",
        "Cycles",
        "IPC",
        "IP/sec",
        "I$ Miss%",
        "D$ Miss%",
        "DRAM RD MB/s",
        "DRAM WR MB/s",
        "Branch Acc%",
    ]

    rows = []
    for r in records:
        name = get(r, "benchmark", default="unknown")
        raw  = r.get("raw",     {})
        drv  = r.get("derived", {})
        rows.append([
            str(name),
            f"{raw.get('cycles', 0):,}",
            f"{drv.get('ipc', 0.0):.4f}",
            f"{drv.get('ip_per_sec', 0.0):.0f}",
            f"{drv.get('icache_miss_rate_pct', 0.0):.2f}",
            f"{drv.get('dcache_miss_rate_pct', 0.0):.2f}",
            f"{drv.get('dram_read_bw_MB_per_sec', 0.0):.2f}",
            f"{drv.get('dram_write_bw_MB_per_sec', 0.0):.2f}",
            f"{drv.get('branch_accuracy_pct', 0.0):.2f}",
        ])

    # Compute column widths
    widths = [max(len(col_names[i]), max(len(row[i]) for row in rows))
              for i in range(len(col_names))]

    sep = "+-" + "-+-".join("-" * w for w in widths) + "-+"
    header = "| " + " | ".join(col_names[i].ljust(widths[i]) for i in range(len(col_names))) + " |"

    print(sep)
    print(header)
    print(sep)
    for row in rows:
        line = "| " + " | ".join(row[i].ljust(widths[i]) for i in range(len(col_names))) + " |"
        print(line)
    print(sep)


# ── Plotting ─────────────────────────────────────────────────────────────────

COLORS = [
    "#4C72B0", "#DD8452", "#55A868", "#C44E52",
    "#8172B2", "#937860", "#DA8BC3", "#8C8C8C",
    "#CCB974", "#64B5CD",
]


def bar_colors(n: int) -> list:
    return [COLORS[i % len(COLORS)] for i in range(n)]


def plot_results(records: list, output_path: str):
    if not HAS_MATPLOTLIB:
        print("[profile_visualize] matplotlib not available — skipping plot.", file=sys.stderr)
        print("[profile_visualize] Install with: pip install matplotlib numpy", file=sys.stderr)
        return

    names = [get(r, "benchmark", default="?") for r in records]
    n = len(names)
    x = np.arange(n)

    # Extract derived metrics
    ipc            = [get(r, "derived", "ipc")                        for r in records]
    ip_per_sec     = [get(r, "derived", "ip_per_sec") / 1e6          for r in records]  # millions/s
    icache_miss    = [get(r, "derived", "icache_miss_rate_pct")       for r in records]
    dcache_miss    = [get(r, "derived", "dcache_miss_rate_pct")       for r in records]
    dram_rd_bw     = [get(r, "derived", "dram_read_bw_MB_per_sec")    for r in records]
    dram_wr_bw     = [get(r, "derived", "dram_write_bw_MB_per_sec")   for r in records]
    branch_acc     = [get(r, "derived", "branch_accuracy_pct")        for r in records]
    sched_stall    = [get(r, "derived", "scheduler_stall_pct")        for r in records]
    rob_stall      = [get(r, "derived", "rob_stall_pct")              for r in records]
    dec_eff        = [get(r, "derived", "decode_efficiency_pct")      for r in records]
    other_stall    = [max(0.0, 100.0 - dec_eff[i] - sched_stall[i] - rob_stall[i])
                      for i in range(n)]

    # ── High Scale Dynamic Optimization Layout Settings ──
    # Dynamically scale the image width so bars have fixed space to render cleanly
    fig_w = max(16, n * 0.35)
    fig_h = 20  # Extra breathing room vertically for vertical text alignments
    
    # Hide individual values if data congestion is too intense (n > 40)
    show_value_labels = (n <= 40)
    
    # Configure font sizes and rotations based on data volume
    tick_rotation = 90 if n > 15 else 35
    tick_alignment = "center" if tick_rotation == 90 else "right"
    tick_fontsize = max(5, min(8, 400 // n)) if n > 30 else 8
    label_fontsize = 6 if n > 20 else 7
    line_thickness = 0.2 if n > 60 else 0.5
    bar_w = 0.75 if n > 30 else 0.6

    fig, axes = plt.subplots(3, 2, figsize=(fig_w, fig_h))
    fig.suptitle("RISCV-OOO Processor Performance Profile", fontsize=18, fontweight="bold", y=0.99)

    colors = bar_colors(n)

    # ── Panel 0: IPC ─────────────────────────────────────────────────────
    ax = axes[0, 0]
    bars = ax.bar(x, ipc, width=bar_w, color=colors, edgecolor="white", linewidth=line_thickness)
    ax.axhline(y=1.0, color="red", linestyle="--", linewidth=1.0, label="Theoretical max (1.0)")
    ax.set_title("Instructions Per Cycle (IPC)", fontweight="bold")
    ax.set_ylabel("IPC")
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=tick_rotation, ha=tick_alignment, fontsize=tick_fontsize)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    if show_value_labels:
        for bar, val in zip(bars, ipc):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.005,
                    f"{val:.3f}", ha="center", va="bottom", fontsize=label_fontsize)

    # ── Panel 1: IP/sec ──────────────────────────────────────────────────
    ax = axes[0, 1]
    bars = ax.bar(x, ip_per_sec, width=bar_w, color=colors, edgecolor="white", linewidth=line_thickness)
    ax.set_title("Instructions Per Second (@ 75 MHz)", fontweight="bold")
    ax.set_ylabel("Millions of Instructions / sec")
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=tick_rotation, ha=tick_alignment, fontsize=tick_fontsize)
    ax.set_ylim(bottom=0)
    ax.grid(axis="y", alpha=0.3)
    if show_value_labels:
        for bar, val in zip(bars, ip_per_sec):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.1,
                    f"{val:.1f}M", ha="center", va="bottom", fontsize=label_fontsize)

    # ── Panel 2: Cache miss rates ─────────────────────────────────────────
    ax = axes[1, 0]
    w = bar_w / 2.2
    b1 = ax.bar(x - w / 2, icache_miss, width=w, label="I-Cache miss %",
                color="#4C72B0", edgecolor="white", linewidth=line_thickness)
    b2 = ax.bar(x + w / 2, dcache_miss, width=w, label="D-Cache miss %",
                color="#DD8452", edgecolor="white", linewidth=line_thickness)
    ax.set_title("Cache Miss Rates", fontweight="bold")
    ax.set_ylabel("Miss Rate (%)")
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=tick_rotation, ha=tick_alignment, fontsize=tick_fontsize)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    if show_value_labels:
        for bar, val in zip(list(b1) + list(b2), list(icache_miss) + list(dcache_miss)):
            if val > 0.01:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.01,
                        f"{val:.1f}", ha="center", va="bottom", fontsize=label_fontsize - 1)

    # ── Panel 3: DRAM memory bandwidth ───────────────────────────────────
    ax = axes[1, 1]
    w = bar_w / 2.2
    b1 = ax.bar(x - w / 2, dram_rd_bw, width=w, label="DRAM read BW",
                color="#55A868", edgecolor="white", linewidth=line_thickness)
    b2 = ax.bar(x + w / 2, dram_wr_bw, width=w, label="DRAM write BW",
                color="#C44E52", edgecolor="white", linewidth=line_thickness)
    ax.set_title("DRAM Memory Bandwidth", fontweight="bold")
    ax.set_ylabel("Bandwidth (MB/s @ 75 MHz)")
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=tick_rotation, ha=tick_alignment, fontsize=tick_fontsize)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    if show_value_labels:
        for bar, val in zip(list(b1) + list(b2), list(dram_rd_bw) + list(dram_wr_bw)):
            if val > 0.1:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.1,
                        f"{val:.1f}", ha="center", va="bottom", fontsize=label_fontsize - 1)

    # ── Panel 4: Pipeline stall breakdown (stacked) ───────────────────────
    ax = axes[2, 0]
    p1 = ax.bar(x, dec_eff,     width=bar_w, label="Decode efficiency %",
                color="#55A868", edgecolor="white", linewidth=line_thickness)
    p2 = ax.bar(x, sched_stall, width=bar_w, bottom=dec_eff,
                label="Scheduler stall %", color="#DD8452", edgecolor="white", linewidth=line_thickness)
    bottom2 = [dec_eff[i] + sched_stall[i] for i in range(n)]
    p3 = ax.bar(x, rob_stall,   width=bar_w, bottom=bottom2,
                label="ROB stall %", color="#C44E52", edgecolor="white", linewidth=line_thickness)
    bottom3 = [bottom2[i] + rob_stall[i] for i in range(n)]
    p4 = ax.bar(x, other_stall, width=bar_w, bottom=bottom3,
                label="Other stall %", color="#8C8C8C", edgecolor="white", linewidth=line_thickness)
    ax.set_title("Pipeline Utilization Breakdown\n(fraction of decode-ready cycles)", fontweight="bold")
    ax.set_ylabel("Fraction (%)")
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=tick_rotation, ha=tick_alignment, fontsize=tick_fontsize)
    ax.set_ylim(0, 110)
    ax.legend(fontsize=8, loc="upper right")
    ax.grid(axis="y", alpha=0.3)

    # ── Panel 5: Branch prediction accuracy ──────────────────────────────
    ax = axes[2, 1]
    bars = ax.bar(x, branch_acc, width=bar_w, color=colors, edgecolor="white", linewidth=line_thickness)
    ax.axhline(y=100.0, color="green", linestyle="--", linewidth=1.0, label="Perfect (100%)")
    ax.set_title("Branch Prediction Accuracy", fontweight="bold")
    ax.set_ylabel("Accuracy (%)")
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=tick_rotation, ha=tick_alignment, fontsize=tick_fontsize)
    ax.set_ylim(
        max(0.0, min(branch_acc) - 5.0) if branch_acc else 0,
        102.0
    )
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    if show_value_labels:
        for bar, val in zip(bars, branch_acc):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.1,
                    f"{val:.1f}%", ha="center", va="bottom", fontsize=label_fontsize)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"[profile_visualize] Plot saved to: {output_path}")
    plt.close(fig)


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Visualize RISCV-OOO profiling results from JSON files."
    )
    parser.add_argument("results_dir",
                        help="Directory containing per-benchmark JSON files")
    parser.add_argument("--out", default="profile_report.png",
                        help="Output PNG path (default: profile_report.png)")
    args = parser.parse_args()

    if not os.path.isdir(args.results_dir):
        print(f"[profile_visualize] ERROR: not a directory: {args.results_dir}",
              file=sys.stderr)
        sys.exit(1)

    records = load_results(args.results_dir)
    if not records:
        print("[profile_visualize] No valid records loaded.", file=sys.stderr)
        sys.exit(1)

    print(f"[profile_visualize] Loaded {len(records)} benchmark(s):\n")

    # ASCII table
    print_ascii_table(records)
    print()

    # Plot
    plot_results(records, args.out)


if __name__ == "__main__":
    main()