#!/usr/bin/env python3
"""Constrained-random stress generator for chiron.

WHY
---
Every one of the four speculation bugs found in this core lived in a corner the
five benchmarks never reach:

  * a long-latency divide still in flight when a mispredict resolves,
  * a second divide waiting behind it while that happens,
  * an MMIO load issued in a branch shadow,
  * an AMO or LR/SC racing a peer hart's line.

Each was found by hand, days apiece, from an event firing about once per 27
million cycles. Directed benchmarks cannot cover that space: they are written to
compute something, and their branches are mostly predictable. This generator
writes programs whose *only* purpose is to sit in those corners, with
data-dependent branches the predictor cannot learn.

ORACLE
------
The generated program is not self-checking, and deliberately so. Single-core
runs go through the existing lock-step harness, which compares every committed
instruction's architectural state against the golden emulator — a far stronger
oracle than any checksum, and it reports the *first* divergence rather than a
wrong answer millions of cycles later. Quad-core runs (no lock-step model yet)
go through profile_quad_check.out, whose per-cycle assertions
(sim/harness/invariants.h) catch the reallocated-slot class directly.

Division by zero is generated on purpose: RISC-V defines it (divu -> all ones,
remu -> dividend) and it is exactly the kind of edge the RTL and the emulator
can disagree about.

REPRODUCIBILITY
---------------
Everything derives from --seed, and the seed is printed and baked into the
generated file as a comment. A failing seed replays with one command; see
`make stress-seed SEED=...`.

Usage:
  tools/gen_stress.py --seed 1234 [--blocks 240] [--out <path.c>]
"""

import argparse
import random
import sys

# Shared-memory footprint. Kept small so it stays in cache and the interesting
# contention is coherence traffic, not capacity misses.
SCRATCH_WORDS = 256
SHARED_WORDS = 16

# CLINT mtime — a read-only MMIO location that is safe to touch speculatively
# from any hart. Reading it in a branch shadow is precisely the pattern that
# went unsquashed in peripheralUnit.
CLINT_MTIME = "0x0200BFF8"


def emit_header(seed, blocks, cores):
    return f"""// mt-stress.c — GENERATED, do not edit by hand.
//
// Constrained-random speculation stress program.
//   seed   = {seed}
//   blocks = {blocks}
//   cores  = {cores}
// Regenerate with:  tools/gen_stress.py --seed {seed} --blocks {blocks}
//
// This program computes nothing meaningful. Its value is the instruction
// sequences it puts the core into: divides and remainders issued inside the
// shadow of data-dependent (therefore unpredictable) branches, speculative MMIO
// reads, and cross-hart AMO/LR-SC contention. Correctness is judged externally
// — by lock-step against the golden emulator (single core), or by the
// microarchitectural assertions in sim/harness/invariants.h (quad core).
//
// The accumulator is carried in a volatile-ish chain so the compiler cannot
// fold the blocks away; every block's result feeds the next block's operands,
// which also means a single wrong speculative result propagates and is caught.

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define SCRATCH_WORDS {SCRATCH_WORDS}
#define SHARED_WORDS  {SHARED_WORDS}

// Per-hart scratch (no false sharing between harts: one 64-byte line each way)
static volatile unsigned long scratch[4][SCRATCH_WORDS]
    __attribute__((aligned(64)));
// Deliberately shared and contended.
static volatile unsigned long shared[SHARED_WORDS] __attribute__((aligned(64)));
static volatile unsigned long results[4];

// xorshift64*, so the operand stream is deterministic but not learnable by a
// branch predictor.
static inline unsigned long nextrand(unsigned long *s) {{
  unsigned long x = *s;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *s = x;
  return x * 2685821657736338717UL;
}}
"""


def block_div_in_shadow(r):
    """A divide issued into the shadow of an unpredictable branch.

    This is the exact shape that wedged the machine: the branch resolves while
    the divider is mid-operation (65 cycles), so the squash path has to age the
    in-flight divide correctly. Two divides back to back also parks the second
    in extnMRequest while the first runs — the window in which the divider's
    branchMask was being clobbered.
    """
    op1 = r.choice(["divu", "div", "remu", "rem", "divuw", "divw", "remuw", "remw"])
    op2 = r.choice(["divu", "remu", "divw", "remw"])
    mask = r.choice([1, 3, 7, 15])
    return f"""  {{
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], {mask}\\n\\t"
        "beq   %[t], zero, 1f\\n\\t"
        "{op1}  %[q], %[s], %[d]\\n\\t"
        "{op2}  %[p], %[q], %[d]\\n\\t"
        "1:\\n\\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }}
"""


def block_nested_branches(r):
    """Nested unpredictable branches — drives branch-mask allocation to its
    limit. branchMaskWidth is 4, so more than four outstanding branches forces
    mask-bit recycling, which is where provenance bugs surface."""
    m1, m2, m3 = (r.choice([1, 3, 7]) for _ in range(3))
    return f"""  {{
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], {m1}\\n\\t"
        "beq   %[t1], zero, 1f\\n\\t"
        "and   %[t2], %[s], {m2}\\n\\t"
        "beq   %[t2], zero, 2f\\n\\t"
        "and   %[t3], %[s], {m3}\\n\\t"
        "beq   %[t3], zero, 3f\\n\\t"
        "divu  %[v], %[s], %[d]\\n\\t"
        "3:\\n\\t"
        "addi  %[v], %[v], 1\\n\\t"
        "2:\\n\\t"
        "addi  %[v], %[v], 2\\n\\t"
        "1:\\n\\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }}
"""


def block_speculative_mmio(r):
    """An MMIO read in a branch shadow.

    peripheralUnit had no branch ageing at all, so a load speculated past a
    mispredict wrote the PRF and readied its ROB entry — and could strand the
    read buffer, blocking every later MMIO access on that hart. That is what a
    dead console looks like.
    """
    mask = r.choice([1, 3, 7])
    return f"""  {{
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], {mask}\\n\\t"
        "beq   %[t], zero, 1f\\n\\t"
        "ld    %[v], 0(%[mm])\\n\\t"
        "1:\\n\\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *){CLINT_MTIME}));
    acc ^= (v >> 3);
  }}
"""


def block_amo(r):
    """Cross-hart AMO contention on a shared line."""
    op = r.choice(["amoadd.d", "amoor.d", "amoxor.d", "amoand.d", "amoswap.d"])
    idx = r.randrange(SHARED_WORDS)
    return f"""  {{
    unsigned long old;
    asm volatile("{op} %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[{idx}])
                 : "memory");
    acc += old;
  }}
"""


def block_lrsc(r):
    """LR/SC retry loop against a contended line — exercises reservation
    tracking, snoop-kill granularity and forward progress."""
    idx = r.randrange(SHARED_WORDS)
    return f"""  {{
    unsigned long v, ok;
    asm volatile(
        "1:\\n\\t"
        "lr.d  %[v], (%[a])\\n\\t"
        "addi  %[v], %[v], 1\\n\\t"
        "sc.d  %[o], %[v], (%[a])\\n\\t"
        "bnez  %[o], 1b\\n\\t"
        : [v] "=&r"(v), [o] "=&r"(ok)
        : [a] "r"(&shared[{idx}])
        : "memory");
    acc += v;
  }}
"""


def block_memory(r):
    """Ordinary loads/stores to per-hart scratch, some under speculation, to
    keep the LSU and D-cache busy alongside everything else."""
    i = r.randrange(SCRATCH_WORDS)
    j = r.randrange(SCRATCH_WORDS)
    return f"""  {{
    scratch[cid][{i}] = acc;
    acc += scratch[cid][{j}];
  }}
"""


def block_fence_i(r):
    """fence.i — historically deadlocked against the D-cache clean-on-fence
    walker, so it belongs in the random mix rather than only in directed tests."""
    return """  asm volatile("fence.i" ::: "memory");
"""


# Weighted so the divide/branch corner dominates, since that is where the
# expensive bugs have been, while everything else still gets regular coverage.
GENERATORS = [
    (block_div_in_shadow, 30),
    (block_nested_branches, 20),
    (block_speculative_mmio, 12),
    (block_memory, 18),
    (block_amo, 8),
    (block_lrsc, 6),
    (block_fence_i, 2),
]


def generate(seed, blocks, cores):
    r = random.Random(seed)
    pool = []
    for fn, w in GENERATORS:
        pool.extend([fn] * w)

    body = []
    for _ in range(blocks):
        body.append(r.choice(pool)(r))
        # Refresh the operands from the PRNG so no two blocks see the same
        # values and the branch outcomes stay unlearnable.
        body.append("  div = (nextrand(&rng) & 0xffff) ;\n")
        body.append("  acc ^= nextrand(&rng);\n")

    out = [emit_header(seed, blocks, cores)]
    out.append("""
void __attribute__((noinline)) stress(int cid, int nc) {
  unsigned long rng = 0x9E3779B97F4A7C15UL ^ ((unsigned long)cid * 0x1000193UL);
  unsigned long acc = (unsigned long)cid + 1;
  unsigned long div = 1;

""")
    out.extend(body)
    out.append("""
  results[cid] = acc;
}

// crt.S enters here with cid in a0 and nc in a1 (NUM_CORES is baked into crt.S
// at build time), matching every other benchmark in this tree.
void thread_entry(int cid, int nc) {
  if (cid >= nc) {
    while (1)
      ;
  }

  // Start together so the AMO/LR-SC blocks actually contend rather than each
  // hart running its section alone.
  barrier(nc);
  stress(cid, nc);
  barrier(nc);

  if (cid != 0)
    exit(2);

  // There is no reference value to compare against: the oracle is external
  // (lock-step vs the emulator, or the invariant assertions). Reaching here
  // with every hart through both barriers is the completion condition; the
  // accumulators are printed only so a divergence is visible by eye in a log.
  uart_send_string("mt-stress: acc=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)(results[i] & 0x7fffffff));
    uart_send_string(i == 3 ? "\\n" : ",");
  }
  exit(0);
}
""")
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, required=True,
                    help="PRNG seed; the whole program derives from it")
    ap.add_argument("--blocks", type=int, default=240,
                    help="number of random blocks to emit (default 240)")
    ap.add_argument("--cores", type=int, default=4)
    ap.add_argument("--out", default="workloads/benchmarks/mt-stress/mt-stress.c")
    a = ap.parse_args()

    text = generate(a.seed, a.blocks, a.cores)
    if a.out == "-":
        sys.stdout.write(text)
    else:
        with open(a.out, "w") as f:
            f.write(text)
        print(f"[gen_stress] seed={a.seed} blocks={a.blocks} -> {a.out}")


if __name__ == "__main__":
    main()
