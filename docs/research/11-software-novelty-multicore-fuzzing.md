# Software-only novelty: fuzzing a coherent multicore, and the oracle problem

> ## ⚠ CORRECTED 2026-09-05 — the headline claims below do NOT survive review.
> Three claims in the original draft are unsafe and must not be published.
> **§0 (added at the end) has the corrections and the defensible residual.**
> Read §0 before using anything in this file.

2026-09-05. Constraint set by the team: a contribution of SMT's weight, but
**implemented in software** (like the lockstep flow), with **minimal RTL
change** — the RTL boots quad-core Linux and is not worth destabilising.

**DAC is an EDA conference.** Verification and design-automation tooling are
core scope there — arguably a better fit than a microarchitecture mechanism.

---

## The gap (verified 2026-09-05)

Every state-of-the-art CPU fuzzer is **single-core**:

| fuzzer | venue | DUT |
|---|---|---|
| [DifuzzRTL](https://lifeasageek.github.io/papers/jaewon-difuzzrtl.pdf) | S&P'21 | single-core, register-coverage guided |
| [TheHuzz](https://www.usenix.org/system/files/sec22-kande.pdf) | USENIX Sec'22 | single-core, golden-model co-sim |
| [ProcessorFuzz](https://arxiv.org/pdf/2209.01789) | — | single-core, CSR-guided |
| [Cascade](https://www.usenix.org/system/files/usenixsecurity24-solt.pdf) | USENIX Sec'24 | single-core, intricate program generation |
| HyPFuzz, TurboFuzz, Lyra | — | single-core |

The one coherence-adjacent bug in that literature is TheHuzz's `fence.i` finding
on CVA6 — **self-modifying code on one hart**, not multi-hart coherence.

**Nobody fuzzes a multi-hart coherent DUT with concurrent threads.** And the
reason is not lack of interest — it is a specific technical blocker.

## Why it hasn't been done: the oracle problem

Single-core fuzzing has an easy oracle: run the program on the DUT and on a
golden ISA model, diff architectural state at each commit. Any difference is a
bug.

**That oracle collapses for a multicore.** With four harts racing, many
different final states are *legitimately* correct — the interleaving is not
determined. A naive diff produces false positives on every run, so the standard
approach is unusable, and the whole field has stayed single-core.

**Solving that oracle is the contribution.** And Chiron has already built the
three pieces, independently, for other reasons:

| layer | what it decides | status |
|---|---|---|
| **race-tolerant differential lockstep** | did any hart diverge architecturally, *excluding legitimately racy commits*? | **exists** — `lockstep_quad.cpp`, reports `racy=`; measured `racy=0` over 57,549 commits on `vvadd-s1-q4` |
| **coherence invariants** | did the machine reach an impossible *state*, regardless of outcome? (SWMR, data-value, dup-way) | **exists** — `swmr_probe`, 0 false positives over 24.4M cycles / ~331K tag transitions, ~6% cost |
| **memory-model conformance** | is the observed outcome inside the set RVWMO *permits*? | **exists** — `mt-litmus` + `herd.logs` allowed-outcome sets |

Layered, these turn non-determinism from a blocker into something decidable:
an outcome is a bug if it diverges non-racily, **or** the machine passed through
an impossible coherence state, **or** the outcome lies outside the memory
model's allowed set. That is a genuine methodological contribution, not an
engineering detail — and **it is exactly the thing the ASP-DAC reviewers said
was missing.**

## Why this answers the rejection directly

R1's objection was: *"XiangShan DiffTest already performs commit-level
differential verification on multicore OoO processors… clarify what is actually
novel in Chiron's verification flow."*

The answer stops being defensive and becomes a claim:

> **DiffTest and every published CPU fuzzer stop at the commit boundary of a
> single instruction stream. The multicore oracle problem — deciding whether a
> non-deterministic outcome is a bug — is unsolved, and it is why no CPU fuzzer
> targets a coherent multicore. We solve it and build the first one.**

The lockstep mechanism is *kept and promoted*: it becomes one layer of a
three-layer oracle rather than the whole claim.

## What has to be built (all software)

1. **Concurrent test-program generator.** Extend `tools/gen_stress.py` from a
   single instruction stream to *N* interacting hart programs, targeting
   speculation × coherence: shared-line read/write patterns, LR/SC and AMO
   contention, false sharing, `fence.i` against peer stores, MMIO under
   speculation.
2. **The layered oracle** (above), unified behind one verdict API.
3. **A multicore coverage metric to steer the fuzzer.** DifuzzRTL needed RTL
   instrumentation for register coverage. **Chiron does not** — the harness
   already reads arbitrary Verilated internals (53 probes do this today), so
   coverage over coherence-protocol transitions × speculation state can be
   computed **host-side with zero RTL change**. That is a real advantage and
   should be stated as one.
4. **Evaluation**: (a) recover the known real bugs by reverting their fixes —
   the CHIRON-BUGS corpus in `04`; (b) find new ones; (c) cost per bug versus
   running the existing gates.

**RTL change required: none.** Optionally a few counters, and even those can be
read host-side instead.

## Risks, honestly

- **The fuzzer must actually find something.** Chiron is now largely clean —
  ISA 84/84, ci-bench 5/5, ci-smp 8/8, Linux boots. If it finds nothing new,
  the paper leans on the corpus recovery, which is weaker. *Mitigation:*
  build the corpus evaluation first so there is a result either way, and note
  that `mt-llist` already fails at ROB 32 / mask 6 (`07`), so the machine is not
  bug-free at all parameter settings.
- **Is a fuzzer "as big as SMT"?** Different axis. SMT is an artifact in an
  ecosystem gap; this is a capability in a methodological gap. Both are "first
  of their kind"; this one is far lower risk and squarely in DAC's scope.
- **Generator quality dominates fuzzing results** (Cascade's whole thesis). A
  weak generator produces a weak paper. This is the part to invest in.
- **Scope discipline.** Do not also try to fuzz BOOM/XiangShan multicore — a
  cross-DUT evaluation is a second paper, not a section.

## Recommended shape

**"Fuzzing coherent multicore processors: a race-tolerant oracle for
speculation × coherence bugs."**

1. The oracle problem, and why CPU fuzzing stopped at one core.
2. The three-layer race-tolerant oracle.
3. Host-side multicore coverage, no RTL instrumentation.
4. The concurrent generator.
5. Evaluation on real bugs (corpus) plus whatever is newly found.
6. The memory-model envelope result (`09`) as a supporting section — same-core
   vs cross-core, model vs U540 silicon vs Chiron.

Everything built this session becomes a component: `swmr_probe` is layer 2,
`mt-litmus` is layer 3, quad lockstep is layer 1, and the corpus is the
evaluation.


---

# §0. CORRECTIONS — what does not survive, and what does

Raised by the team as reviewer bait, then checked. **All three objections are
correct, and one is worse than flagged.**

## Unsafe claim 1 — "DiffTest does not do multicore"

**False, and the ASP-DAC reviewer already cited the paper.** XiangShan's
DiffTest has supported multicore since ~2021 (Y. Xu et al., MICRO 2022). The
draft above says *"DiffTest and every published CPU fuzzer stop at the commit
boundary of a single instruction stream"* — that conflates DiffTest (multicore,
commit-level) with the fuzzers (single-core). **Never write this.**

**Defensible version:** DiffTest's oracle is a *commit-boundary architectural
diff*. It decides from architectural state at commit. It therefore cannot decide
anything when there are no commits (a hang), and it fires only once a wrong
value has reached a committed architectural register.

## Unsafe claim 2 — "the first fuzzer to target a coherent multicore"

**False. Two decades false.**

- **[TSOtool, ISCA 2004](http://xenon.stanford.edu/~hangal/tsotool.pdf)** runs
  *pseudo-randomly generated multiprocessor programs with data races* on a
  shared-memory system and checks the results against the formal TSO
  specification, using a novel polynomial-time algorithm (full TSO checking is
  NP-complete). It found real bugs in shipping Sun systems.
- **[McVerSi, HPCA 2016](https://users.cs.utah.edu/~vijay/papers/hpca16.pdf)**
  is a genetic-programming test-generation framework for memory-consistency
  verification in full-system simulation, with a crossover function that
  *prioritises memory operations contributing to non-determinism*. That is
  guided fuzzing of a multicore, explicitly.
- **MTraceCheck** validates non-deterministic MCM behaviour in post-silicon.

**Never claim the "first multicore fuzzer".**

## Unsafe claim 3 — "the multicore oracle problem is unsolved"

**False.** TSOtool's polynomial-time checker *is* a solution to the multicore
oracle problem for TSO; `herd` supplies allowed-outcome sets for RVWMO; our own
quad lockstep with `racy=` and `mt-litmus` are instances of the same idea. The
draft's framing writes those out of existence.

## What actually survives

The distinction is **not** novelty of multicore testing. It is **what the oracle
can see**:

> Every prior multicore oracle — TSOtool, McVerSi, MTraceCheck, DiffTest — is
> **value/outcome-based**: it decides legality from observed values, and needs
> the program to produce them. That leaves two blind spots, both of which this
> project hit repeatedly on real RTL:
>
> 1. **Liveness failures** — a hang produces no values to check. Four of the
>    hardest bugs here were hangs.
> 2. **Latent microarchitectural-state violations** — dual-Unique put the same
>    line in Unique state in two L1s at once; the wrong *value* surfaced ~10⁸
>    cycles later as a Linux `/init` hang, and five directed reproducers passed
>    on both the buggy and fixed RTL.
>
> A **microarchitectural-state layer**, checked on RTL cycle by cycle
> (`swmr_probe`: SWMR + data-value + dup-way), fires at the transaction that
> causes the violation rather than at the outcome that eventually reveals it.

That is honest, it is defensible, and it is **exactly the reframing the team
proposed**: DiffTest's commit diff is layer 1; layers 2–3 catch what never shows
up as a non-racy commit mismatch.

## Consequence for venue — say this plainly

With those corrections the contribution is **oracle layering and detection
latency**, evaluated on a real-bug corpus. That is a good **DATE / TCAD / ITC**
paper and an excellent **section** of a larger one.

**It is not a DAC headline.** It is a refinement of prior oracles, not a new
capability, and a reviewer who knows TSOtool will say so.

**Therefore: SMT (`10`) remains the stronger bet for DAC 2027,** and this work
belongs inside it as the verification-credibility section — which is also its
natural role, since a speculation-touching change like SMT cannot be claimed
correct without exactly these layers.

## The one residual that is arguably still open

TSOtool and McVerSi target **memory-consistency** bugs and are value-based.
Most of the CHIRON-BUGS corpus is *not* MCM violations — it is deadlock,
starvation, reallocated ROB/PRF slots, and lost stores, none of which a
completing-program value check would catch. So *"guided random testing for
speculation × coherence **liveness and microarchitectural-state** bugs on RTL"*
is a narrower claim that may still be open.

**But it is the same paper as `01`–`06`, which was already judged
section-sized.** Do not re-pitch it as a headline.
