# Layer 1 (SWMR) — prototype and false-positive validation

2026-09-05, branch `research`. Prototype: `sim/harness/probes/swmr_probe.cpp`,
built by `make build/swmr_probe.out`.

This records the **first half** of validating an oracle: showing it does not cry
wolf. It does *not* yet show the oracle catches anything — that is the
true-positive experiment, below, and until it is done this is a checker with no
demonstrated detection power.

---

## What it checks

Per `(set, tag)`, every cycle, across all four L1 D-caches:

| invariant | violated when |
|---|---|
| **SWMR** | two harts hold the line valid-and-not-shared (Unique) at once — the literal dual-Unique signature of CO-1 |
| **MULTI-DIRTY** | two harts hold the line valid-and-dirty; reported separately from SWMR so a *shared-bit* bug is distinguishable from an *ownership* bug |
| **DUP-WAY** | one hart holds the same tag valid in two ways of a set — targets the fill/replacement races of CO-7 |

Violations are **timed, not just counted**: each is opened when first seen and
closed when it clears, and the run prints a histogram of durations. The reason
is stated in §"Result 2".

### Geometry correction (worth its own note)

The existing probes in `sim/harness/probes/` hardcode **128 sets, `tagSize` 19,
`tagSection` 23**. That was right when the D-cache was 32 KB. It is now 64 KB:
**256 sets, `tagSize` 18, `tagSection` 22**, confirmed against freshly generated
Verilog (`tagChunks_0 = tagBRAM_rdData[21:0]`, `reg [87:0] mem [0:255]`). Any
probe copied from the old ones is silently decoding the wrong bits. `swmr_probe`
derives the geometry from the same formulas as `cacheLookupUnit.scala` and
`static_assert`s the result, so a future resize breaks the build instead of the
measurement.

---

## Result 1 — no false positives, on a model exercised hard

All runs on `research` @ `main` (RTL rebuilt from this branch; probe binary
verified newer than `Vsystem__ALL.a`).

| workload | cycles | tag-state changes | distinct lines | SWMR | MULTI-DIRTY | DUP-WAY |
|---|---:|---:|---:|---:|---:|---:|
| `mt-vvadd-s1-q4` | 400 K | 529 | 320 | 0 | 0 | 0 |
| `mt-llist-q4` | 6 M | 26,090 | 4,065 | 0 | 0 | 0 |
| `mt-seqlock-q4` | 6 M | **160,148** | 20 | 0 | 0 | 0 |
| `mt-lrsc-q4` | 6 M | 139,716 | 678 | 0 | 0 | 0 |
| `mt-crosscall-q4` | 6 M | 4,158 | 275 | 0 | 0 | 0 |
| **total** | **24.4 M** | **~331 K** | | **0** | **0** | **0** |

`mt-seqlock` is the discriminating case: **160,148 tag-state transitions over
only 20 distinct lines**, i.e. those lines are transferred between harts
constantly. SWMR held on every one.

**The census column exists because a checker that observes nothing always
reports CLEAN.** The first `vvadd-s1-q4` run looked clean at only 529
set-checks, which was not obviously distinguishable from a probe reading the
wrong memory. The census (`distinct lines ever valid`, `ways valid at end`)
proves the tag arrays being read are the ones the cache actually uses — and it
also revealed that `vvadd-s1-q4` is a *weak* test: 316 of its 320 lines were
still resident at the end, so nothing was ever evicted and there was almost no
sharing. That is why the SMP micros were run instead of stopping at the first
green result.

## Result 2 — no transient violations at all (unexpected)

The prototype was built expecting spurious alarms: during a fill or a snoop
downgrade there could plausibly be cycles where two caches momentarily look like
owners before the loser's tag updates. The duration histogram and the
`--persist` threshold exist to handle exactly that.

**Zero transient episodes were observed** — not one, across 24.4 M cycles and
~331 K tag transitions. The RTL never exposes an intermediate dual-owner state
in the tag arrays.

This matters more than it first appears: it means the invariant is **binary**,
needs no persistence threshold, no tuning, and no "ignore short windows"
heuristic. Any firing is a real state violation. Keep the timing machinery
anyway — it costs nothing and it is the evidence for this claim.

## Result 3 — the check is essentially free

Same binary, same I/O, `--nocheck` disabling only the invariant, 3 reps each on
`mt-seqlock-q4` for 400 K cycles:

| | rep 1 | rep 2 | rep 3 | mean |
|---|---:|---:|---:|---:|
| `--nocheck` | 10.96 s | 10.71 s | 10.71 s | **10.79 s** |
| checking | 11.50 s | 11.40 s | 11.38 s | **11.43 s** |

**~6% simulation overhead.** Cheap enough to leave on by default.

(An earlier single-pair measurement showed checking as *faster* than
`--nocheck`; that was contention from a concurrent run, not a result. Repeating
it is what made the number usable — cf. memory `sim-throughput-contention-trap`.)

The cost is low because the per-cycle work is 1,024 `memcmp`s of 12 bytes
against a shadow copy, and a set is only re-decoded when one of its rows
actually changed. That is sound: a line occupies the same set index in every
cache, so a violation can only be created by a write to one of the
participating rows.

---

## What this does NOT show

**No true positive has been demonstrated.** Everything above says the checker is
quiet on a good model; none of it says the checker is loud on a bad one. A
checker that always returns CLEAN would produce this exact table.

**The decisive experiment** — and it is now known to be feasible: `bc4a4ab`
(*"never answer a snoop from a fence.i walker writeback"*, the CO-1 fix)
**reverts cleanly** on today's tree — 6 files, 18 insertions, 59 deletions.

The experiment:

1. Revert `bc4a4ab`, rebuild, run `swmr_probe` on `mt-fencei-q4` and the other
   micros.
2. The historical record says **five directed reproducers passed on both the
   buggy and the fixed RTL** (`mt-fencei` in three shapes, `mt-llist`,
   `mt-crosscall`) — the architectural oracle could not discriminate at all, and
   the bug was only found by hand-dumping tag state after a ~10⁸-cycle Linux
   boot hang.
3. **If SWMR fires on a workload that still PASSES, that is the paper's thesis
   demonstrated in one experiment**: the outcome is right, the state is wrong,
   and only the cycle-level structural oracle can tell.

If it does *not* fire on the short micros, that is also a real finding — it
would mean the buggy window needs the `fence.i` walker to race a peer store in a
way the micros never produce, and the honest conclusion is that Layer 1 needs
the Linux workload to detect CO-1. Report whichever happens.

## Also still open

- **DVI is not implemented.** SWMR is checkable from tag state alone; the
  data-value invariant needs the coherence *order* of writes, so it needs the
  golden model underneath. CO-6 (L2 MSHR refill patched from a live wire) is
  pure data corruption with correct coherence state — **SWMR cannot catch it**,
  by construction. Do not let Result 1 imply Layer 1 is finished.
- Layer 2 (slot-ownership tags) not started.
- Only the D-cache is checked; the I-cache is not in the invariant.
