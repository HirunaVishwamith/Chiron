# Where the novelty actually is

Written 2026-09-05, after reading the ASP-DAC 147 reviews and checking the
literature. This file argues for one direction and records why the obvious
alternatives were rejected. Claims about prior work are cited; where I could not
verify something, it says so.

---

## 1. Directions that are dead, and why

Before proposing anything, rule out what will fail again.

### Dead: "a faster / better OoO RISC-V core"

Chiron's decode, issue and commit are all **1-wide**, so per-core IPC is capped
at 1.0 by construction. Measured single-core IPC is 0.293–0.692. BOOM and
XiangShan are 3–5 wide superscalar; the C910 comparison study reports a **119.5%
IPC improvement** for OoO C910 over scalar CVA6 in 22nm silicon
([Ramping Up Open-Source RISC-V Cores, CF'25](https://arxiv.org/abs/2505.24363)).
We cannot win a performance comparison, and a paper whose headline is
performance will be measured on exactly that axis. **Do not write a performance
paper.**

### Dead: "our lockstep co-verification flow"

R1 is right. [XiangShan DiffTest](https://github.com/OpenXiangShan/difftest) is
the deployed, published state of the art for commit-level differential testing
of a multicore OoO RISC-V core, and the group has already moved past it —
DiffTest-H (MICRO 2025) *"expands the verification states to 32 architectural
behaviors while reducing the communication overhead to 0.4%"*
([DOI](https://doi.org/10.1145/3725843.3756108)). Competing head-on with the
XiangShan verification group on their own ground is not a fight this project
wins.

### Dead: "we added deadlock/liveness assertions"

Progress-counter and liveness-to-safety assertion techniques are standard
industrial formal practice
([Codasip](https://codasip.com/2023/09/26/formal-verification-best-practices-investigating-a-deadlock/)),
and [AutoSVA](https://arxiv.org/pdf/2104.04003) already automates liveness
property generation for RTL module interactions. Our `WEDGED` assertion is good
engineering, not a contribution.

### Dead: "memory-consistency verification methodology"

[RTLcheck (MICRO'17)](https://dl.acm.org/doi/10.1145/3123939.3124536) and
[RealityCheck](https://arxiv.org/pdf/2003.04892) already cover automated
microarchitectural MCM verification, including RVWMO. We must *use* litmus
testing, not claim to have invented it. (Note the gap this leaves open — §3.)

---

## 2. The one thing we have that nobody else does

Over 18 months this project produced an unusually complete record of **real
bugs in a speculative, coherent, out-of-order multicore RTL** — each one hunted
to a cycle-exact root cause, fixed, and documented with a minimal reproducer.
Roughly 20 of them are written up in the project's memory notes, and the fixes
are individual commits in `git log`. They cluster into a taxonomy:

- **Reallocated-slot class** (4 independent instances) — a completion lands on a
  ROB/PRF slot that was rolled back and reassigned to a different, younger
  instruction. Example: ACE `responseBuffer` squash clobber, where
  `regRecordUpdate` last-connect resurrects a squashed load's `branch.valid` on
  MSHR pop, so a stale load writes a reallocated physical register.
- **Coherence-window class** — a line reaches two harts in Unique state at once;
  snoops answered out of a `fence.i` walker writeback hand peers pre-store data
  with `PassDirty`; an in-flight `CleanUnique` completing *after* invalidation
  resurrects a stale line and erases a peer's committed store; word-granular
  snoop kill silently loses an LR/SC update.
- **Liveness class** — CCU snoop starvation; ROB coherent-squash livelock;
  D-cache arbiter atomic-window lockout freezing the whole CCU; per-core request
  starvation in the coherency path; a silently dropped D-cache request wedging
  the issuing hart forever.

Now the important part. **Our own verification stack repeatedly failed to catch
these**, and we have the receipts:

- A branch-window guard **passed 84/84 riscv-tests while deadlocking all five
  quad-core benchmarks** at exactly 500,000 cycles.
- An `injFSM` escape fix **passed riscv-tests 84/84, all five benchmarks, and
  two open reproducers — and still corrupted memory and killed the Linux boot**
  (memory: `injfsm-escape-fix-corrupts-memory`). The note's own conclusion: *"a
  green suite does not validate a speculation-path change."*
- The lost `csd_unlock` store was traced to a specific store at cycle
  **871,254,973** — i.e. the fault became architecturally visible roughly a
  billion cycles into a Linux boot.

That last point is the crux, and it is where DiffTest's design meets its limit.

### The claim worth making

> Commit-level differential testing detects a bug **only when, and only where,
> it becomes architecturally visible at a commit boundary.** For the dominant
> bug classes in a speculative coherent multicore, that is either
> **catastrophically late** (10⁵–10⁹ cycles after the faulting event, in a
> different hart, in a different subsystem) or **never** (a hang produces no
> commit to compare, and a lost store may simply change a result no one checks).

This is not a rhetorical claim — it is measurable, on real bugs, and nobody has
measured it. The literature confirms the gap directly:

- The CPU-fuzzing community's own evaluations complain that real bug corpora are
  unobtainable. Of the HACK@EVENT competitions of the last five years, **only 3
  of 10 released their corpus, and only 28 of 170 released bugs came with
  descriptions or fixes**; *"manually inserting realistic bugs into real-world
  designs is complex and demands deep understanding of the design and nature of
  bugs"* ([Encarsia, USENIX Sec'25](https://comsec-files.ethz.ch/papers/encarsia_sec25.pdf)).
  Encarsia's entire contribution is **automatic bug injection** — a workaround
  for the fact that real bugs are scarce.
- The state-of-the-art CPU fuzzers ([DifuzzRTL](https://lifeasageek.github.io/papers/jaewon-difuzzrtl.pdf),
  [ProcessorFuzz](https://arxiv.org/pdf/2209.01789),
  [Cascade](https://www.usenix.org/system/files/usenixsecurity24-solt.pdf)) are
  **single-core** and oriented toward architectural/CSR/security divergence.
  None of them targets the speculation × coherence interaction, and none can
  observe a livelock.
- RTLcheck's multicore MCM evaluation was performed on **V-scale — a 3-stage,
  in-order, 32-bit core.** To my knowledge no open **speculative out-of-order**
  RISC-V multicore has been validated against RVWMO litmus tests on RTL. *(Not
  exhaustively verified — check XiangShan and BOOM publications before claiming
  this in print.)*

### Therefore: the contribution is the corpus and the measurement, not the tool

**CHIRON-BUGS: an open, reproducible corpus of real speculation–coherence bugs
in an out-of-order RISC-V multicore, and a measurement of what each verification
technique actually catches.**

Each bug ships as: the one-commit revert that reintroduces it, a minimal
multi-hart reproducer, the cycle-exact root cause, and the fix. Then run the
whole verification matrix against every bug and report, per technique:

| axis | meaning |
|---|---|
| **detection rate** | does it catch the bug at all? |
| **detection latency** | cycles between the faulting event and the first alarm |
| **localization distance** | how far (cycles / modules / harts) from the alarm to the root cause |
| **cost** | simulation slowdown |

Techniques on the matrix: `riscv-tests` · benchmark result checking ·
single-hart architectural lockstep (the DiffTest-equivalent baseline) ·
quad-hart lockstep with race tolerance · cycle-level microarchitectural
invariants (`ci-check`) · RVWMO litmus tests · random speculation stress ·
SMP Linux boot.

The expected headline — which the `injFSM` and `csd_unlock` cases already
foreshadow — is that **the architectural oracle catches a minority of these bugs
and, when it does, does so 10³–10⁹ cycles downstream, whereas cycle-level
structural invariants catch them within tens of cycles of the faulting event.**
If the data says otherwise, that is still a publishable result and we report it.

**Why this is defensible where the old paper was not:** it converts R1's
DiffTest objection from a threat into *our baseline*, converts R2's "correctness
under multicore corner cases is not demonstrated" into *the evaluation itself*,
and stops depending on beating BOOM on IPC.

---

## 3. Secondary contributions available from the same repo

These strengthen the paper; none carries it alone.

**(a) Quantifying the composable-interface tax (answers R3 and B2 directly).**
We can now price uniform ready/fire handshaking with real numbers, because
removing dead handshake states in the CCU was worth **2.35× on the Linux boot**
and 1.07–1.65× across benchmarks. Pair that with a genuine module swap —
BTB-only vs full TAGE, a replacement-policy change, an interconnect FSM
replacement — done without touching neighbours, and both halves of R1/R3's
composability complaint are answered with data. All the A/B switches already
exist in `common/configuration.scala`
(`enableAdvancedPredictor`, `enableTAGE`, `enableRAS`, `branchMaskWidth`,
`robAddrWidth`, `disableFenceIWalker`).

**(b) A speculation-depth / MLP finding.** Measurement says the **branch mask,
not the ROB, is the memory-level-parallelism limiter** — and that growing the
ROB to 32 with a 6-bit branch mask gains ~2% but **livelocks `mt-llist`**
(memory: `mlp-limiters-and-spec-depth-trap`). "Speculation capacity is bounded
by tag width, not window size, and widening it trades IPC for liveness" is a
genuine, non-obvious microarchitectural result and ties straight back to the
bug-corpus theme.

**(c) The artifact itself.** An open **AXI-ACE snoop-coherent, out-of-order,
quad-core** RISC-V that boots SMP Linux on RTL and on FPGA appears to occupy an
empty spot: BOOM is OoO but TileLink and effectively single-core;
[Culsans](https://arxiv.org/pdf/2407.19895) is ACE-style snoop coherence but on
**in-order** CVA6; OpenPiton+CVA6 is in-order. That is an artifact claim, not a
research claim — but it is worth one honest sentence, and it makes Culsans the
natural comparison point (§4).

---

## 4. Positioning against other processors (answers B4)

**Hard constraint: Chiron is RV64IMA — there is no F/D floating point.** This
rules out most of Splash-3, PARSEC, and SPEC. Any plan that assumes them is
wrong. Workable options:

| workload | why | comparability |
|---|---|---|
| **CoreMark**, **Dhrystone** | integer, tiny, universally published | direct CoreMark/MHz table vs BOOM, CVA6, Rocket, C910 |
| **Embench-IoT** | integer, standard, designed for small cores | published numbers exist for several open cores |
| **Splash-3 `radix`** (+ other integer kernels) | the suite Culsans and OpenPiton report | direct multicore comparison on the same kernel |
| **RVWMO litmus tests** | coherence/consistency conformance | the only apples-to-apples *correctness* comparison |
| **SMP Linux boot** | already works on RTL | few open OoO multicores demonstrate this |

Culsans is the most useful single baseline: it reports *"up to 32.87% faster in
a dual-core setup and an average improvement of 15.8% over OpenPiton"* on
Splash-3, so matching its methodology gives us a comparison the reviewers will
recognise.

**Honest caveat to write into the paper:** against a superscalar OoO core we
will lose on IPC. Say so, in the paper, in one sentence, with the single-issue
reason. A stated limitation is far cheaper than a reviewer discovering it.

---

## 5. Venue

Ranked by realism, given that the contribution is a verification/characterisation
result rather than a performance mechanism:

1. **DATE / ICCAD / ITC** — the natural home. DATE in particular takes
   verification-methodology and design-flow papers, and R3's complaint ("for an
   EDA conference, no discussion on design flow, verification, or DSE") is
   literally a description of what DATE wants.
2. **IEEE TCAD / ACM TODAES (Q1 journals)** — the *best* fit for a corpus +
   measurement study, because the page budget fits ~20 bug case studies and an
   artifact. Slower, but no novelty-vs-page-limit squeeze.
3. **MICRO / HPCA** — only if secondary contribution (b) is developed into a
   real mechanism with a real result. Do not target these on the corpus alone.
4. **ASPLOS / ISCA** — not realistic for this work.

Recommendation: **target DATE or a Q1 journal (TCAD/TODAES) with the corpus
paper.** Treat HPCA/MICRO as reachable only via a follow-on mechanism paper.
