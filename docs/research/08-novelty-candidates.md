# Novelty search — candidates checked, and the recommendation

2026-09-05. Goal: find a novelty to implement on Chiron. Method changed after
this morning: **check each candidate against the literature BEFORE pitching it**,
because every idea proposed earlier today turned out to be occupied.

---

## 1. Candidates checked and rejected (five for five)

| candidate | occupied by | verdict |
|---|---|---|
| Selective coherent-load replay instead of full ROB flush | [Cain & Lipasti, ISCA 2004](https://pharm.ece.wisc.edu/papers/isca2004cain.pdf) — value-based replay eliminates **95%** of consistency squashes in MP configs; [InvisiFence, ISCA 2009](https://dl.acm.org/doi/10.1145/1555815.1555785) | **dead** |
| "Chiron's full-ROB flush is unusually crude" | Intel memory-ordering machine clear ("nuke" on snoop hit in the load buffer) — the flush *is* the textbook mechanism, patented since P6 | **dead** |
| Adaptive speculation throttling under contention | [US10073784B2](https://patents.google.com/patent/US10073784B2/en) — same mechanism, trigger is HTM abort rate | **too close** |
| HTM on RISC-V reusing LR/SC + OoO rollback machinery | [arXiv 2510.15888](https://arxiv.org/pdf/2510.15888) (Oct 2025) — HTM generalizing LR/SC, no ISA or coherence-protocol change, L1-only hardware | **freshly dead** |
| Invisible speculation as a multicore *performance* win | InvisiSpec (MICRO'18) already evaluates **10 PARSEC** workloads alongside 23 SPEC; GhostMinion, InvarSpec, SpecBox follow | **premise false** |

**What this pattern means.** Core-microarchitecture mechanisms are a mature,
crowded field defended by very large teams. A six-author project with a 1-wide
OoO core is not going to win a novelty contest there, and today is five pieces
of evidence for that. The realistic novelty is not *"nobody thought of this
mechanism"* — it is **"nobody could measure this, because nobody had this
platform."**

## 2. What Chiron uniquely is

- A **speculative out-of-order** multicore with **real coherence** that **boots
  SMP Linux**, small enough to modify wholesale, with 164 counters, a golden
  model, quad-hart lockstep, and now a cycle-level coherence invariant checker.
- Every relevant knob is a build parameter: `branchMaskWidth`, `robAddrWidth`,
  `prfAddrWidth`, snoop-filter size, TAGE/RAS on/off.
- Uses **AXI-ACE**, not TileLink — unusual in RISC-V.
- **No MMU** (nommu Linux), which is unusual for an OoO Linux-capable machine.

The research questions that need *exactly* this are about what speculation does
to a multicore's observable behaviour — and those are measurable, not
inventable.

## 3. RECOMMENDED: the memory-model envelope of speculation

> **Claim: out-of-order speculation widens the set of memory-model behaviours a
> multicore actually exhibits, and speculation depth is the knob that controls
> it — up to the point where architectural guarantees break.**

Nobody has this data for RISC-V. Verified gap:

- **[RTLcheck (MICRO'17)](https://dl.acm.org/doi/10.1145/3123939.3124536)** —
  the only *measured RTL* MCM work, evaluated on **V-scale: 3-stage, in-order,
  32-bit**, 56 litmus tests, sequential consistency.
- **[RealityCheck](https://arxiv.org/pdf/2003.04892)** — covers RVWMO and OoO
  pipelines, but **formally**, over microarchitectural ordering *specifications*
  — not measured execution of real RTL.
- No RVWMO conformance study of **BOOM** or **XiangShan** found.

So: **there is no measured RVWMO conformance characterisation of a speculative
out-of-order RISC-V multicore's RTL.** Chiron can produce the first one, and the
assets are already downloaded: **9,932 litmus tests**, `model-results/herd.logs`
(the allowed-outcome sets), and — unplanned but valuable —
`hw-results/SiFive-Freedom-U540.log`, outcomes from **real silicon**.

### Three parts, increasing in value

1. **Conformance.** Run litmus on the RTL; check observed ⊆ allowed. Any
   observed-but-not-allowed outcome is an RVWMO violation and a bug worth its
   own section. This alone answers R2's *"correctness under multicore corner
   cases is not adequately demonstrated."*
2. **The envelope, as a function of speculation depth.** For each test, measure
   *which* allowed relaxed behaviours are actually observed, and how often, at
   `branchMaskWidth` ∈ {2,4,6} × `robAddrWidth` ∈ {4,5}. The hypothesis is that
   the envelope widens monotonically with speculation depth — that a deeper
   window makes a machine *architecturally more relaxed in practice*. Compare
   against the U540 silicon column: an in-order-ish core should show a narrower
   envelope than Chiron at any depth.
3. **The correctness cliff, and a mechanism.** Push depth until an architectural
   *guarantee* breaks — the constrained-LR/SC forward-progress requirement is
   the sharpest one, and Chiron has four historical brushes with it. Then build
   the mechanism that preserves the guarantee without giving back the depth.
   That is the implemented novelty, and it is *derived from measurement* rather
   than guessed.

### Why this survives review

- It is a **first**, and verifiable as one.
- It needs a platform almost nobody has: OoO + coherent + open + resizable + a
  golden model. Simulators cannot answer it (they don't exhibit implementation
  behaviour); in-order cores cannot (no speculation); commercial silicon cannot
  (parameters are fixed).
- It converts the reviewers' complaints into the paper's evaluation.
- It does not depend on beating BOOM on IPC — which we cannot do at 1-wide.

### Honest risks

- Part 1 may be **boring-clean**: conformance holds and nothing is found. Then
  the paper rests on parts 2–3.
- Part 2's hypothesis may be **flat** — the envelope may not widen measurably at
  these small depths (branch mask 4→6 is not a large change).
- Part 3 rests on the **unverified** `32/bm6` livelock observation, which has a
  known confound (see `07-novelty-check-speculation-coherence.md`).
- We must write a `.litmus` → bare-metal RV64 generator; `litmus7` is not
  available and the repo's `elf-tests/` are not it.

## 4. Alternatives, ranked below it

- **Platform + measured contradictions.** Four RTL findings that contradict what
  a simulator study would conclude: I$ capacity ×8 buys 7%; the CCU is
  latency-bound at 54% busy and deeper pipelining *regresses*; branch-mask width
  not ROB depth limits MLP; the store gate is 100% of ROB-head stall and is FSM
  turnaround, not miss latency. Solid, honest, DATE/TCAD-shaped. Weaker as
  "novelty", stronger as "true".
- **CHIRON-BUGS corpus + oracle placement.** Analysed in `01`–`06`. Verification
  paper; DATE/TCAD realistic. Layer 1 exists and is validated for false
  positives.
- **ACE vs TileLink on RISC-V.** Thin on its own; a section, not a paper.

## 5. De-risking step, before committing

Two cheap experiments, both under a day, no night job:

1. **`32/bm6` + `ci-smp`**, with `mt-lrsc` phase 2 as the control — settles
   whether the correctness cliff is real or was the `mt-llist` testbench bug.
2. **One litmus test end to end** — hand-port a single `.litmus` (e.g.
   `BASIC_2_THREAD/LB+ctrl+po`) to bare metal, run it on two harts, and check the
   outcome against `herd.logs`. Proves the whole part-1 pipeline before we build
   a generator for 9,932 of them.

If (2) works and (1) is real, the direction is fully de-risked and the rest is
execution.
