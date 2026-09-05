# The idea: coherence-aware lockstep, and the latency-debt argument for it

Decided 2026-09-05. Direction: bug-corpus paper, **lockstep retained and
extended, not replaced**. Target: DAC 2027 (~mid-November 2026).

The instruction that shaped this document: *keep the lockstep mechanism — it is
a crucial component for bug fixing — and find a novel idea to build on top of
it, while addressing everything the reviewers pointed out.*

That is the right constraint, and it makes the contribution sharper than
discarding lockstep would have. Lockstep is not the weak part of this project.
The weak part was claiming lockstep *itself* as the novelty when DiffTest and
Dromajo already exist. So: keep the machine, move the claim.

---

## 1. The reframing, in one paragraph

A verification oracle is defined by two things: **where it observes** and **what
invariant it checks**. Commit-level differential co-simulation — DiffTest,
Dromajo, and our own lockstep — observes at the *architectural commit boundary*
and checks *architectural state equality*. That placement is what makes it
portable and cheap. It is also what makes it late.

Every bug has an **activation cycle** (the cycle the hardware first does
something wrong) and a **manifestation cycle** (the first cycle an oracle can
see it). Call the gap **latency debt**. Debugging cost is paid in latency debt,
not in bug count: the engineer must search backwards across it. For the
speculation × coherence bug classes in this repository, the debt at the commit
boundary is enormous — and sometimes infinite, because a hang produces no commit
to compare and a lost store may never change a checked result.

**The paper's argument: the commit boundary is the wrong place to stand for this
bug class, and we can prove it with real bugs and measure what standing
lower costs and buys.**

---

## 2. Evidence the debt is real (already in hand, before any new work)

| observation | latency debt |
|---|---|
| A branch-window guard passed **84/84 riscv-tests** and deadlocked all five quad benchmarks at exactly 500,000 cycles | ∞ at the architectural oracle — a deadlock never commits |
| An `injFSM` escape fix passed riscv-tests 84/84, all 5 benchmarks, and 2 open reproducers — then corrupted memory and killed the Linux boot | ~10⁹ cycles, and only under a workload no gate ran |
| The lost `csd_unlock` store became architecturally visible at cycle **871,254,973** | ~10⁸–10⁹ cycles |
| Dual-Unique line coherence violation — snoops answered out of a `fence.i` walker writeback handed peers pre-store data | manifested as a Linux `/init` hang, i.e. as a liveness failure, not a value mismatch |

Four of the hardest bugs in this project's history were **hangs**. A
commit-comparison oracle is structurally blind to all four: it compares commits,
and there are none.

---

## 3. The mechanism (this is the novel part)

Extend the golden model from an *architectural* reference into a **coherence-
and speculation-aware** reference, and run the comparison at the ACE transaction
boundary and the allocation boundary as well as at commit. Concretely, three
layers stacked on the existing lockstep:

### Layer 1 — Shadow coherence directory (the main contribution)
The golden model gains a per-line directory reconstructed *from the RTL's own
observed ACE transactions*: which hart holds the line, in which state, and which
transaction is in flight. Two textbook invariants are then checkable every cycle
(Sorin/Hill/Wood):

- **SWMR** — single writer, multiple readers: no two harts hold the same line
  Unique/Modified simultaneously.
- **Data-value invariant** — a read returns the value written by the most recent
  write in the line's coherence order.

This is precisely the pair of properties our worst bugs violated. Dual-Unique is
a literal SWMR violation. Stale-`CleanUnique`-resurrects-a-line and
word-granular-snoop-kill are literal DVI violations. **Each would have been
caught at the transaction that caused it, instead of ~10⁸ cycles later — or, in
the dual-Unique case, instead of never.**

### Layer 2 — Slot-ownership tracking
Every ROB/PRF slot carries the identity of the instruction that allocated it;
every completion is checked against that identity at the write port. This is the
generalized form of the *reallocated-slot defect class* — the bug shape that
recurred **four independent times** in this design. It is a one-line structural
check that closes an entire class, and the recurrence count is the evidence that
the class is real and not an artifact of one careless module.

### Layer 3 — RVWMO conformance
Litmus tests run on the RTL, outcomes checked against the RVWMO axiomatic model.
The `litmus-tests-riscv` repository ships `model-results/`, so this needs no
herdtools/OCaml install. This layer answers "is the memory model right?", which
neither lockstep nor coherence invariants ask, and it is the only
*apples-to-apples correctness comparison* available against other cores.

**Lockstep is the foundation of all three.** Layers 1–2 need a trusted
architectural reference to say what the *correct* value and the *correct* owning
instruction were; without lockstep there is nothing to compare against. The
contribution is not a replacement — it is pushing the oracle *below* the commit
boundary while keeping the commit-boundary check as the backstop.

---

## 4. Why this is defensible against the prior work R1 cited

| prior work | where it observes | why it does not cover this |
|---|---|---|
| **DiffTest** (XiangShan, MICRO'22) | architectural state at commit | blind to liveness bugs; debt = manifestation latency; no coherence-protocol state |
| **DiffTest-H** (MICRO'25) | 32 *architectural* behaviors, low-overhead FPGA transport | expands *which architectural* states, not *where* the oracle stands; still commit-boundary, still architectural |
| **Dromajo** (MICRO'21) | architectural co-simulation incl. BOOM | same placement; single-core-oriented |
| **RTLcheck** (MICRO'17) / **RealityCheck** | formal, SVA generated per litmus test | formal and offline; multicore evaluation was on **V-scale**, a 3-stage *in-order* core — not a speculative OoO multicore |
| **DifuzzRTL / ProcessorFuzz / Cascade** | architectural/CSR divergence, coverage-guided | **single-core**; cannot observe a livelock; target security/ISA bugs, not speculation × coherence |
| industrial UVM coherence scoreboards | protocol conformance | check the protocol in isolation; not cross-checked against an architectural golden model, and not published as a reproducible artifact |

The unoccupied square: **a runtime, cycle-level, coherence- and
speculation-aware differential oracle for a speculative out-of-order multicore,
evaluated on real bugs.**

> **Before print:** this table is built from five targeted literature searches,
> not a systematic sweep. Do a proper related-work pass (DiffTest-H full text,
> recent XiangShan verification papers, MICRO/ISCA/DAC 2024–2026 on
> co-simulation and coherence checking) before asserting the gap in a
> submission. Do not let a reviewer find the paper we missed.

---

## 5. What makes this a *result* rather than a *tool*

Three deliverables, in increasing order of value:

1. **CHIRON-BUGS** — an open corpus of real, reproducible speculation–coherence
   bugs, each with revert patch, minimal multi-hart reproducer, cycle-exact root
   cause, and fix. The literature says this is the scarce artifact: of ten
   HACK@EVENT competitions only three released a corpus, and only 28 of 170 bugs
   came with a description or fix; Encarsia (USENIX Sec'25) resorts to
   *automatic* bug injection because real bugs are unobtainable.

2. **The latency-debt measurement** — every bug × every oracle, reporting
   detection rate, detection latency in cycles, localization distance, and
   simulation cost. Expected headline: the commit-level oracle catches a
   minority and pays 10³–10⁹ cycles of debt when it does; the coherence-aware
   oracle catches most within tens of cycles. **If the data says otherwise we
   report that** — a negative result here is still the first measurement of its
   kind.

3. **New bugs.** Run the coherence-aware oracle on stress, litmus, and the Linux
   boot and see what it catches that nothing else did. Finding even one or two
   previously-unknown bugs in our own design converts the paper from "we
   measured a thing" to "we measured a thing and it worked." This is the single
   highest-value experiment in the plan and should not be left to the end.

---

## 6. How this answers each reviewer complaint

| complaint | how it is answered |
|---|---|
| R1: novelty vs DiffTest/Dromajo | They become the *baseline* on the latency-debt table. The claim moves from "we have lockstep" to "here is where lockstep's oracle placement fails, measured." |
| R1/R2: how are harts 1–3 checked? | Quad-hart lockstep already exists (`racy=0` over 57,549 commits) and is now described properly — plus Layer 1 checks cross-hart coherence directly, which is the real answer. |
| R2: coherent-load recovery correctness not demonstrated under corner cases | Layer 1 + litmus *is* that demonstration, and the mechanism appears in the corpus as bugs it once had. |
| R2: no verification baselines | The latency-debt table is nothing but verification baselines. |
| R2/R3: only 5 kernels, no standard workloads | CoreMark, Dhrystone, Embench-IoT, Splash-3 integer kernels, litmus (§Phase 2). |
| R1: scaling measured only against ourselves | Cross-processor comparison vs BOOM/CVA6+Culsans/OpenPiton on shared workloads. |
| R1/R3: composability asserted, never demonstrated or priced | Real module swaps + the measured handshake tax (the CCU dead-state removal was worth 2.35× on the Linux boot — R3 was right, and we say so). |
| R3: no design-flow / verification / DSE discussion for an EDA venue | The whole paper is a verification-flow result; the DSE sweep is correctness-constrained (widening the branch mask gains ~2% IPC and **livelocks `mt-llist`**). |

Every complaint lands somewhere. That is the test a resubmission has to pass.

---

## 7. Feasibility check (2026-09-05) — Layer 1 is simpler than designed

Before committing to the mechanism, I checked whether the state it needs is
actually observable. It is, and more directly than §3 assumed.

`sim/harness/probes/lrsc_wedge_probe7.cpp` already reads, **per core, per set,
per way, every cycle**, straight out of the Verilated model:

```c
#define CORE_T(n)     ...core##n##__DOT__memAccess__DOT__cacheLookup__DOT__tagBRAM__DOT__mem
#define CORE_D(n, w)  ...core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_##w##__DOT__mem
```

and decodes the flags from the tag entry (`cacheLookupUnit.scala:429-432`):
`V = tagSize`, `dirty = tagSize+1`, `shared = tagSize+2`, `PLRU = tagSize+3`,
with `tagSection = 4 + tagSize`.

**Consequence: the shadow directory does not need to be reconstructed from ACE
transactions at all.** SWMR can be checked against ground truth directly —
scan the four L1 tag arrays and assert that no line is held Unique
(`valid && !shared`) by two cores at once. That is exactly the condition that
was hand-dumped to diagnose CO-1 (`V1 M1 S0` in two L1s simultaneously); the
contribution is turning that one-off manual dump into a continuous, automatic,
cycle-level invariant.

This removes the main implementation risk. It also means the probe
infrastructure is already most of the way there: **53 probes** exist under
`sim/harness/probes/`, and `lrsc_wedge_probe7` is described in its own header as
a *"line-state change tracker with STALE-FILL detector (golden lastValid
compare)"* — i.e. a single-line, hand-aimed prototype of Layers 1 and its
data-value check. Generalizing it from one set to all sets, from one line to
all lines, and from "print on change" to "assert an invariant" **is** the
mechanism.

**Cost estimate:** 4 cores × 128 sets × 4 ways = 2048 way-entries to diff per
cycle in the naive form. Tractable in C++ but it will slow simulation; the
obvious optimizations are (a) maintain an incremental shadow keyed by tag and
only re-check lines whose tag entry changed, and (b) make the full sweep
periodic with the incremental check continuous. Measure the slowdown — it is a
column in the results table, not an implementation detail.

**Honest caveat:** reading the tag arrays gives *cache state*, which makes SWMR
directly checkable. The data-value invariant additionally needs to know the
coherence *order* of writes, which the tag arrays alone do not give. For DVI,
the golden model's memory image remains the reference (this is where lockstep is
load-bearing, exactly as §3 argued), and in-flight transactions still have to be
tracked. Do not let the SWMR result make DVI look free.
