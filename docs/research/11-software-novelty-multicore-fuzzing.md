# Software-only novelty: fuzzing a coherent multicore, and the oracle problem

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
