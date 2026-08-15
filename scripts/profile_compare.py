#!/usr/bin/env python3
"""Compare quad-core profile JSON against a committed baseline.

Exit 0 if every requested bench is present, completed, and within the
cycle/IPC budgets. Exit 1 on a miss, a verify failure, or a regression.

    python3 scripts/profile_compare.py testdata/baseline/q4 build/profile_results
    python3 scripts/profile_compare.py testdata/baseline/q4 build/profile_results \\
        --only vvadd-q4 --max-cycle-regress 0.03
"""

from __future__ import annotations

import argparse
import json
import os
import sys


DEFAULT_BENCHES = ("vvadd-q4", "matmul-q4", "filter-q4", "histo-q4", "csaxpy-q4")

# Unscaled compare name -> the scale `make profile-quad` / the sweep uses.
# Matches mk/benchmarks.mk *_DEFAULT_SCALE. A sweep writes fam-sN-q4.json;
# profile-quad writes fam-q4.json. Accept either.
DEFAULT_SCALE = {
    "vvadd": 1,
    "matmul": 1,
    "filter": 1,
    "histo": 5,
    "csaxpy": 5,
}


def result_json(current_dir: str, name: str) -> str | None:
    """Return the first existing result path for a baseline name like vvadd-q4."""
    direct = os.path.join(current_dir, f"{name}.json")
    if os.path.isfile(direct):
        return direct
    if name.endswith("-q4"):
        fam = name[:-3]
        scale = DEFAULT_SCALE.get(fam)
        if scale is not None:
            scaled = os.path.join(current_dir, f"{fam}-s{scale}-q4.json")
            if os.path.isfile(scaled):
                return scaled
    return None


def load(path: str) -> dict:
    with open(path) as fh:
        return json.load(fh)


def get(d: dict, *keys, default=None):
    cur = d
    for k in keys:
        if not isinstance(cur, dict) or k not in cur:
            return default
        cur = cur[k]
    return cur


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("baseline_dir", help="directory of committed *-q4.json files")
    ap.add_argument("current_dir", help="directory of freshly written *-q4.json files")
    ap.add_argument(
        "--only",
        nargs="+",
        default=None,
        help="subset of benchmark names (default: all five q4 families)",
    )
    ap.add_argument(
        "--max-cycle-regress",
        type=float,
        default=0.03,
        help="fail if max_cycles grows by more than this fraction (default 0.03)",
    )
    ap.add_argument(
        "--max-ipc-drop",
        type=float,
        default=0.03,
        help="fail if C0 IPC falls by more than this fraction (default 0.03)",
    )
    args = ap.parse_args()

    benches = tuple(args.only) if args.only else DEFAULT_BENCHES
    failed = 0

    header = (
        f"{'bench':12} {'base cyc':>10} {'now cyc':>10} {'dCyc%':>8} "
        f"{'C0 base':>8} {'C0 now':>8} {'status'}"
    )
    print(header)
    print("-" * len(header))

    for name in benches:
        base_path = os.path.join(args.baseline_dir, f"{name}.json")
        now_path = result_json(args.current_dir, name)
        if not os.path.isfile(base_path):
            print(f"{name:12} {'':>10} {'':>10} {'':>8} {'':>8} {'':>8} FAIL missing baseline")
            failed += 1
            continue
        if not now_path:
            print(f"{name:12} {'':>10} {'':>10} {'':>8} {'':>8} {'':>8} FAIL missing result")
            failed += 1
            continue

        base = load(base_path)
        now = load(now_path)
        base_cyc = get(base, "aggregate", "max_cycles", default=0) or 0
        now_cyc = get(now, "aggregate", "max_cycles", default=0) or 0
        base_ipc = get(base, "cores", default=[{}])
        now_ipc_l = get(now, "cores", default=[{}])
        base_c0 = (base_ipc[0].get("derived") or {}).get("ipc", 0.0) if base_ipc else 0.0
        now_c0 = (now_ipc_l[0].get("derived") or {}).get("ipc", 0.0) if now_ipc_l else 0.0
        dcyc = 100.0 * (now_cyc - base_cyc) / base_cyc if base_cyc else 0.0

        reasons = []
        result = get(now, "result") or {}
        bench_error = result.get("bench_error", -1)
        harness_exit = result.get("harness_exit", 0)
        complete = result.get("complete", True)
        if bench_error not in (-1, 0):
            reasons.append(f"bench_error={bench_error}")
        if harness_exit not in (0,):
            reasons.append(f"harness_exit={harness_exit}")
        if complete is False:
            reasons.append("incomplete")
        if base_cyc and now_cyc > base_cyc * (1.0 + args.max_cycle_regress):
            reasons.append(f"cycles +{dcyc:.2f}%")
        if base_c0 and now_c0 < base_c0 * (1.0 - args.max_ipc_drop):
            reasons.append(f"C0 IPC {now_c0:.3f}<{base_c0:.3f}")

        status = "FAIL " + ", ".join(reasons) if reasons else "ok"
        if reasons:
            failed += 1
        print(
            f"{name:12} {base_cyc:10d} {now_cyc:10d} {dcyc:+7.2f}% "
            f"{base_c0:8.4f} {now_c0:8.4f} {status}"
        )

    print()
    if failed:
        print(f"profile_compare: {failed} failure(s)")
        return 1
    print("profile_compare: all benches within budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
