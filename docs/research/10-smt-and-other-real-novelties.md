# Real novelty candidates — SMT, and the alternatives

2026-09-05. Written after five *mechanism* ideas were each found occupied
(`08-novelty-candidates.md`), and after the direction was correctly pushed back
on: measurement studies and verification tooling are **sections**, not the
headline for DAC/HPCA-class venues.

---

## 0. What "real novelty" actually means here

The reference class is BOOMv3/SonicBOOM, RiscyOO (MIT composable OoO), and
Culsans (CVA6 coherency unit). It is worth being precise about what those
papers actually claim, because it sets a reachable bar:

- **None of them invented a new concept.** Snoop coherence was not new when
  Culsans was built. Modular hardware design was not new for RiscyOO. BOOMv3 was
  a redesign of BOOMv2.
- **All of them built a substantial open artifact in a documented gap, and
  evaluated it properly.**

So the target is not "nobody has ever thought of this". It is:

> **a substantial piece of hardware, filling a gap the open-source ecosystem
> demonstrably has, evaluated in a way only we can.**

---

## 1. PRIMARY RECOMMENDATION — SMT on the out-of-order core

**Build 2-way simultaneous multithreading into Chiron's OoO core: 4 cores × 2
threads = 8 harts.**

### The gap (verified 2026-09-05)

| core | SMT? |
|---|---|
| BOOM / SonicBOOM | no |
| XiangShan | no |
| CVA6 / CVA6S+ | no |
| C910 | no |
| RiscyOO | no |

The only RISC-V SMT work found is *"Implementation of a RISC-V SMT Core in an
AI processor"* (SoICT 2022) — a **closed, proprietary** control core inside
ArchiTek's "Chichibu" edge-AI chip, in a heterogeneous virtual-engine
architecture. Not open, not an application-class OoO core.

**There is no open-source simultaneously-multithreaded out-of-order RISC-V
core.** That is structurally the same gap Culsans filled ("CVA6 has no
coherency unit").

### Why Chiron, from its own measurements

The motivation is measured, not asserted:

| measurement | value | source |
|---|---|---|
| per-core IPC against a hard 1.0 ceiling | **0.29 – 0.69** | 46-run sweep |
| ROB-head stall, coordinator cores | **62 – 68%** | `profile_quad` |
| ROB head waiting on memory (`hnr_load`) | 21 – 35% | csaxpy quad |
| store gate share of `rob_ready_blocked` | **100%** | `store_gate_probe` |

Decode/issue/commit are 1-wide, so **30–70% of issue slots are empty**, and the
dominant cause is a stalled ROB head waiting on memory. That is exactly the
condition SMT converts into throughput: thread A stalls, thread B issues.

**This turns the reviewers' strongest criticism into the paper's motivation.**
R3 said IPC is low versus BOOM. At 1-wide we cannot answer that by getting
faster. We can answer it by showing what the empty slots are worth.

### The evaluation nobody else can run

Chiron is parameterised on core count, so all three of these can be built on
the same FPGA, same memory system, same workloads:

- 4 cores × 1 thread — today's baseline
- **4 cores × 2 threads** — SMT
- **8 cores × 1 thread**

→ **throughput per LUT / BRAM, iso-area.** This answers *"for a small OoO
RISC-V multicore, is SMT better than more cores?"* — and it is unanswered for
open RISC-V OoO **because nobody has both halves**. That is the headline result.

### The second result, which is uniquely ours

Two SMT threads share an L1, so a lock handoff between them costs **zero
coherence traffic** — an L1 hit instead of a cross-core ping-pong. Given that
the CCU is latency-bound (54% busy, queue depth 0.24) and coherence has been
this project's dominant cost, **SMT is also a coherence-reduction mechanism**.

Measurable: coherence transactions per synchronisation operation, same-core vs
cross-core threads → a thread-placement result.

The litmus work then slots in *underneath* as a supporting section: same-core
threads have a different observable memory-model envelope than cross-core
threads, and `mt-litmus` already measures exactly that. Section, not headline —
which is where it belongs.

### Honest risk

**Per-thread flush is the danger.** Squashing thread A must not kill thread B,
and that is precisely where all four [[reallocated-slot]] bugs lived.

Mitigating synergy: Chiron's squash machinery is **already tag-based** (branch
masks plus a coherence tag). Adding a thread dimension extends machinery the
team knows intimately, having debugged four instances of its failure mode. The
PRF is already 64 entries against a 16-entry ROB, so there is headroom. And
`swmr_probe`, `ci-smp` and quad lockstep now exist to catch regressions.

Work items:
1. Per-thread architectural state — PC, rename map, CSRs, commit point.
2. Thread-tag or partition ROB / issue queue / PRF / branch masks.
3. **Per-thread flush** (the risky one).
4. Fetch policy — ICOUNT is the standard starting point.
5. Verification: lockstep extended to 8 harts; `ci-smp` must gate.

### Staging for ~10 weeks (aggressive but plausible)

| weeks | goal |
|---|---|
| 1–3 | 2-way SMT on a **single** core, two bare-metal threads, proven |
| 4–6 | 4 cores × 2 threads; `ci-smp` green; 8-hart lockstep |
| 7–8 | iso-area study: synthesise 4×2 and 8×1 |
| 9–10 | writing |

**Fallback if SMT is not running by week 5:** publish single-core SMT plus the
measurement studies. Smaller paper, still a paper.

---

## 2. Alternatives explored

### 2a. Vector unit (RVV) on an out-of-order core — REAL GAP, TOO BIG

Open RVV implementations attach to **in-order** hosts: Ara→CVA6, Vitruvius,
Spatz, Saturn→Rocket/Shuttle. BOOM has no V. XiangShan has no V. So *"a vector
unit integrated with an OoO core"* is a genuine, checkable gap.

**But the implementation cost is far beyond 10 weeks** (vector register file,
LMUL/VL handling, masking, memory ops, exceptions on vector state). Record it
as the natural *next* paper, not this one.

### 2b. Heterogeneous (asymmetric) OoO multicore — PLAUSIBLE GAP, LOW EFFORT, WEAKER CLAIM

Instantiate the four cores with **different microarchitectures** on one coherent
fabric — differing ROB depth, branch-mask width, cache geometry, predictor —
then study asymmetry-aware placement.

Searching found ESP (heterogeneity at the **SoC/accelerator** tile level) and
BlackParrot-BedRock (coherence system), but nothing with **asymmetric core
microarchitectures on one coherent fabric** for open RISC-V.

- **Effort: low.** Per-core parameterisation mostly exists; it needs to become
  per-*instance* rather than global.
- **Bonus:** directly answers R1's *"composability is only described at the
  interface level — demonstrate an actual modification"*. This *is* that
  demonstration.
- **Weakness:** it is a configuration exercise plus a study, not new hardware.
  Good **second contribution** inside the SMT paper; thin as a headline.

### 2c. Load-store queue + store buffer, and its memory-model cost — GOOD FALLBACK

Chiron has **no post-commit store buffer**; stores serialise at the ROB head
(100% of `rob_ready_blocked`, ~37% of runtime on vvadd core 0). Building a real
LSQ with disambiguation and store-to-load forwarding is substantial hardware.

On its own *"we added an LSQ"* is catching up to BOOM, not novelty. **But**
paired with the litmus finding it becomes a claim: adding the store buffer
should widen the observable memory-model envelope to include LB, which
`09-envelope-first-results.md` predicts and `mt-litmus` can verify. That is a
measured performance-vs-relaxation trade-off on real RTL.

Best role: **the fallback headline if SMT stalls**, or a strong section of the
SMT paper (SMT threads sharing a store buffer is itself a memory-model
question).

---

## 3. Rejected, with evidence

| candidate | why dead |
|---|---|
| Selective coherent-load replay | Cain & Lipasti ISCA 2004 — 95% of consistency squashes eliminated |
| "Our full ROB flush is unusually crude" | It *is* the textbook mechanism (Intel MOMC) |
| Adaptive speculation throttling | US10073784B2 (HTM-abort trigger) |
| HTM via LR/SC + OoO rollback | arXiv 2510.15888, **Oct 2025** |
| Invisible speculation as a multicore *win* | InvisiSpec already evaluates 10 PARSEC workloads |
| **Spectre defence implemented in RTL** | **Occupied**: SAVP claims the first RTL prediction-based defence on an open RISC-V core (Chisel, 3.3% area / 7.2% power); also SPECCFI, SSE-RV, and CARRV'19 BOOM mitigations |
| Interconnect as the headline | Team judgement: the interconnect is the weakest part of the design |

---

## 4. Recommendation

**Primary: SMT (§1), with heterogeneous-core instantiation (§2b) as the second
contribution and the memory-model envelope (§2c, `09`) as a supporting section.**

That gives one paper with a substantial hardware artifact, an evaluation only
this platform can run (iso-area SMT vs more cores), a demonstrated answer to the
composability criticism, and a memory-model result underneath — while the
verification work (`swmr_probe`, quad lockstep, `ci-smp`) becomes the credibility
argument rather than the claim.

**Next decision point:** the design document for §1 — which structures get
thread-tagged versus partitioned, where per-thread flush must be exact, and the
fetch policy.
