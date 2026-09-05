# The memory-model envelope: first measured results

2026-09-05, branch `research`. Harness:
`workloads/benchmarks/mt-litmus/mt-litmus.c`, `make litmus-bin LITMUS=SB|LB
FENCE=0|1 ITERS=n`.

All runs on the **fixed** model (`grep -c retain sim/rtl/system.v` = 185) with
`build/profile_quad_fast.out` **relinked and mtime-verified** against
`Vsystem__ALL.a`. (The first SB run was accidentally taken against the
bc4a4ab-reverted library left over from the CO-1 experiment; it gave the same
answer, but that was luck. Re-run properly — the numbers here are the good ones.)

---

## The three columns

For each litmus test there are three sets of final states:

| column | source | meaning |
|---|---|---|
| **allowed** | `model-results/herd.logs` | what RVWMO *permits* |
| **silicon** | `hw-results/SiFive-Freedom-U540.log` | what a real chip *exhibits* (4× in-order U54, 1.2×10⁹ iterations) |
| **chiron** | this harness | what *this* RTL exhibits |

A state outside **allowed** is an RVWMO violation — a bug. A state inside it is
not: an implementation is free to be *stronger* than the model.

## Results

### SB — store buffering
`P0: x=1; r0=y` ‖ `P1: y=1; r1=x`, relaxed outcome `r0=0 ∧ r1=0`.

| | relaxed observed | iterations |
|---|---|---|
| RVWMO (herd) | **allowed** (Positive: 1, "Sometimes") | — |
| SiFive U540 | **never** (Positive: 0, 3 of 4 states seen) | 1.2×10⁹ |
| **Chiron** | **1** — `OBSERVED` | 2,000 |
| Chiron + `fence rw,rw` **[control]** | **0** | 2,000 |

Full Chiron histogram (no fence): `(0,0)=1  (0,1)=1745  (1,0)=218  (1,1)=36`.
With the fence: `(0,0)=0  (0,1)=1995  (1,0)=0  (1,1)=5`.

**The control is what makes this a measurement.** The fence removes the relaxed
outcome *and* collapses the distribution, so what was seen is real hardware
reordering, not a harness artefact.

### LB+ctrl — load buffering with a control dependency
`P0: r0=x; ctrl; y=1` ‖ `P1: r1=y; x=1`, relaxed outcome `r0=1 ∧ r1=1`.

| | relaxed observed | iterations |
|---|---|---|
| RVWMO (herd) | **allowed** (Positive: 1) | — |
| SiFive U540 | **never** (Positive: 0) | 1.2×10⁹ |
| **Chiron** | **0** | 2,000 |
| Chiron + fence **[control]** | 0 | 2,000 |

Chiron histogram: `(0,0)=490  (0,1)=753  (1,0)=757  (1,1)=0`.

**This zero is meaningful, not a dud run.** The other three states are evenly
populated, so the harness is genuinely interleaving the two harts; LB simply is
not exposed.

---

## The finding: the envelope is asymmetric, and the asymmetry is explained

```
allowed  ⊇  chiron  ⊋  U540        on SB   (store side)
allowed  ⊇  chiron  =  U540        on LB   (load side)
```

Chiron is **wider than in-order silicon on SB and not on LB**, and its
microarchitecture predicts exactly that:

- **SB `(0,0)` needs a load to complete without seeing the peer's store.**
  Chiron issues loads **speculatively, out of order**, so a load can execute
  before an older store in the same hart has committed. That is the reordering
  SB detects — and note it is exposed by *load* speculation, despite the test's
  name.
- **LB `(1,1)` needs a store to become visible before an older load has
  completed.** Chiron cannot do this: stores are committed **in order at the ROB
  head** — there is no post-commit store buffer — so no store can overtake an
  older load. LB is structurally unreachable.

So the same structural weakness that costs Chiron ~37% of runtime on `vvadd`
(the store gate, 100% of `rob_ready_blocked`) also makes it **accidentally
stronger than RVWMO requires** on the load-buffering side.

## The prediction this makes — and it is testable

**Adding the post-commit store buffer should widen the envelope to include LB.**

That is a directly measurable causal link between a *performance* optimisation
and *observable memory-model relaxation*, on real RTL. And a partially-built
store buffer already exists on `perf/sota-coherent-interconnect` (attempted
twice, reverted, blocker documented in memory `store-gate-arbiter-serialization`).

If that holds, the paper's claim is not merely "OoO is more relaxed than
in-order" — it is:

> **The observable memory-model envelope is a microarchitectural design
> parameter. Optimisations move it, we can measure which ones and by how much,
> and the trade-off is quantifiable on real RTL.**

## Caveats to fix before any of this is published

1. **The harness synchronisation is weak.** A 4-hart `barrier()` between
   iterations costs ~2,200 cycles and is poor litmus practice; real harnesses
   use tight sync plus randomised delays. The SB *observation* stands (it
   happened, and the fence control removes it), but **the 1/2000 rate is not a
   trustworthy number.** Tighten before quoting a rate.
2. **Two tests is not an envelope.** This needs the generator across a real
   subset of the 9,932 tests, at minimum the classic families (SB, LB, MP, CoRR,
   ISA2, R, S, 2+2W) with and without each fence variant.
3. **Only harts 0 and 1 participate**; harts 2–3 sit in the barrier. Real tests
   should also run on other hart pairs — the CCU is not symmetric (core 0 is the
   coordinator in every existing benchmark).
4. **No U540 methodology match.** They ran 1.2×10⁹ iterations; we ran 2×10³.
   Absence of an outcome at 2,000 iterations is *not* evidence of absence.
   The LB zero needs far more iterations before it can be called a zero.
