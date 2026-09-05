# Comparison with existing work

The ASP-DAC reviewers' second objection, after novelty, was that the paper did
not position itself against anything. This file is the answer, and it is written
to be defensible rather than flattering: it starts from what other tools
actually do, states plainly where Kairos is *not* competitive, and separates
claims we can demonstrate from claims we can only argue.

The rule adopted here, after this project already had to retract three
overreaching claims (see `11-software-novelty-multicore-fuzzing.md` §0): **never
characterise a paper we have not read, and never say "first" about anything.**

---

## 1. The two axes that matter

Almost all dynamic hardware verification varies **the program** and observes
**the architectural result**. Kairos varies **the schedule** of a fixed program
and observes **microarchitectural state**. Those are orthogonal, which is the
whole positioning argument — it is a complementary axis, not a better fuzzer.

| | Varies the **program** | Varies the **schedule** |
|---|---|---|
| **Observes the architectural result** | ISA fuzzers, differential testing, MCM test generation | rare (a schedule change alone usually does not change a legal result) |
| **Observes microarchitectural state** | coverage-directed RTL fuzzing | **Kairos** |

---

## 2. The landscape

Grouped by what each one is *for*. Claims here are limited to what the tools do
by construction; where a detail matters to our argument and we are not certain
of it, it is marked **[verify]** rather than asserted.

### 2a. Differential / lockstep testing — "did the committed state match a reference?"

* **Spike / Dromajo lockstep**, and this project's own `make lockstep` /
  `lockstep-q4`. Compares committed architectural state against a golden model.
* **DiffTest** (XiangShan). Commit-level differential testing, and it **does**
  support multicore — a claim we previously got wrong and retracted.

**Relationship to Kairos.** This is exactly oracle **layer 1**, and Kairos keeps
it rather than competing with it. The gap is not that differential testing is
weak; it is that a commit comparison is silent on two things by construction:
a **hang** produces no committed state to compare, and an **illegal
microarchitectural state** produces no mismatch until (and unless) it
eventually corrupts a committed value. Both of this project's hardest bugs were
of that shape — the dual-Unique coherence violation manifested as a Linux
`/init` hang roughly 10⁸ cycles later, in a different subsystem.

Layers 2 and 3 exist for precisely that gap. That is the honest framing, and it
is the one to put in the paper: *DiffTest's commit diff is layer 1; layers 2–3
catch what never becomes a non-racy commit mismatch.*

### 2b. Memory-consistency test generation — "is the memory model respected?"

* **TSOtool** (Hangal et al., ISCA 2004). Runs pseudo-random racy
  multiprocessor programs and checks the observed values against formal TSO with
  a polynomial-time algorithm.
* **McVerSi** (Elver & Nagarajan, HPCA 2016). Genetic-programming test
  generation for memory-consistency verification, explicitly selecting for
  non-determinism.
* **MTraceCheck** (Lee & Bertacco, ISCA 2017) **[verify]**. Validating
  non-deterministic memory-consistency behaviour in post-silicon validation.
* **litmus / herd / diy** (Alglave, Maranget et al.). Hand- and
  machine-generated litmus tests with formally enumerated allowed outcomes.

**Relationship to Kairos.** This family is the closest prior work and the paper
must say so up front. They vary the **program** (and, in McVerSi's case,
deliberately select programs that behave non-deterministically) and check
**observed values** against a memory model. They do not control *when* each
core runs, they do not observe cache state, and they do not reduce a failing
run to a minimal cause.

Kairos is the complement: fix the program, vary the schedule, observe the
microarchitecture, minimise the result. This project already runs litmus tests
(`mt-litmus`, `09-envelope-first-results.md`) and keeps them — an SB relaxed
outcome that a SiFive U540 never produced in 1.2×10⁹ iterations is observable
on Chiron, which is a memory-model result and belongs in that section, not this
one.

### 2c. Coverage-directed RTL fuzzing — "which RTL states have we reached?"

* **RFUZZ** (Laeufer et al., ICCAD 2018). Mux-toggle coverage, FPGA-accelerated.
* **DifuzzRTL** (Hur et al., IEEE S&P 2021). Register-coverage-guided CPU fuzzing
  with differential comparison.
* **TheHuzz** (USENIX Security 2022) **[verify]**, **Cascade** (Solt et al.,
  USENIX Security 2024) **[verify]**. Instruction-level fuzzing for CPU bugs,
  Cascade via intricate long program generation.

**Relationship to Kairos — and where we are explicitly not competitive.** These
generate programs to reach new RTL states, and they are largely **single-core**
in their published evaluations. We do not claim to beat them at program
generation, we do not implement mux/register coverage, and a paper claiming
Kairos "outperforms DifuzzRTL" would be comparing incomparable things. The
composition to state instead: *a program fuzzer supplies the programs; Kairos
supplies the schedules each program is executed under.* They multiply.

### 2d. Software concurrency testing — where the schedule idea comes from

* **PCT** (Burckhardt et al., ASPLOS 2010). Randomised scheduling with a
  probabilistic guarantee of finding depth-*d* bugs: ≥ 1/(n·k^(d−1)).
* **CHESS** (Musuvathi & Qadeer, OSDI 2008 / iterative context bounding, PLDI
  2007) **[verify]**. Systematic, bounded exploration of thread interleavings.
* **Delta debugging** (Zeller & Hildebrandt, IEEE TSE 2002). Minimising a
  failure-inducing input.

**Relationship to Kairos.** This is the intellectual debt and the paper should
be generous about it: the *ideas* are theirs, the contribution is transferring
them to RTL and confronting what breaks in the transfer. What breaks is
specific and measurable, and is the most interesting part of the work:

1. **No blocking signal.** Software PCT runs the highest-priority *enabled*
   thread; the runtime knows when a thread blocks. Hardware has none, and "has
   not retired" is not a substitute — a hart spinning on a flag retires forever.
   Measured: without bounded leadership tenure, `pct:3` left two harts with zero
   retired instructions in 662,901 cycles.
2. **`k` is enormous.** In software, *k* is the number of scheduling steps; in
   hardware every cycle is one, so the bound weakens dramatically. Whether the
   effective *k* should instead be the number of shared-memory events is an open
   question we should measure rather than assert.
3. **Bug depth is not obviously the same notion** for a coherence race as for a
   data race on a shared variable.
4. **Delta debugging gets *easier*, not harder.** Software shrinkers fight a
   flaky predicate. Here the DUT is deterministic and the candidate schedule is
   fully specified, so the predicate is a pure function — every candidate is
   tested once and the answer never changes. The same determinism that makes RTL
   simulation blind to interleavings is what makes reduction over them exact.
   That inversion is worth a sentence in the abstract.

---

## 3. What we can actually run as a baseline

Being honest about this is more persuasive than a table of numbers we cannot
reproduce. We do not have TSOtool, McVerSi or DifuzzRTL running on Chiron, and
porting them is not a two-week job. So the evaluation compares against what is
genuinely available, and says so:

| Baseline | Status | What it shows |
|---|---|---|
| **Deterministic regression, repeated** | runs, `deterministic` policy | The thesis: 8 runs explore **1** interleaving, on every workload |
| **Random delay injection** | runs, `random:p` policy | The honest strawman — roughly what a UVM testbench does with randomised delays |
| **PCT, transferred** | runs, `pct:d` policy | Whether the software-derived policy earns its complexity |
| **Windowed delay** | runs, `windowed:w` policy | Whether wide contiguous delays beat per-cycle coin flips |
| Commit-level lockstep | already in-tree (`make lockstep-q4`) | Oracle layer 1, and the layer-2/3 gap is measured against it |
| Litmus / herd | already in-tree (`mt-litmus`) | Memory-model envelope, separate contribution |
| TSOtool / McVerSi / DifuzzRTL | **not run** | Positioned qualitatively; no numbers claimed |

Measured, 4 policies × 3 workloads × 8 runs (see `12-schedule-exploration.md`):

| Workload | `deterministic` | `random:0.02` | `pct:3` | `windowed` |
|---|---:|---:|---:|---:|
| mt-illegal | **1** | 1 | 7 | 4 |
| mt-spinwait | **1** | 3 | 8 | 6 |
| mt-crosscall | **1** | 8 | 8 | 5 |

Two results here are worth more than a win: `random` is genuinely weak on short
workloads (1/8 on mt-illegal) because a per-cycle coin flip targets nothing; and
`pct` leads on distinct schedules while producing *fewer* distinct cross-hart
order pairs on mt-crosscall (48 vs 144), because serialising harts explores
different schedules without exploring different orderings. Reporting only
"distinct schedules" would have hidden that — which is why the coverage model
carries two metrics.

---

## 4. Claims to make, and claims to refuse

**Make:**

* A deterministic RTL simulator explores one interleaving per program, and we
  measure exactly that (the `deterministic` column).
* Controlled hart delay recovers schedule variation pre-silicon, with a
  soundness property: a stall can only produce states the unmodified design
  could reach unaided, so there is no false-positive class from the mechanism.
* The RTL cost is four lines behind a config knob, and with the knob off the
  netlist is byte-identical to the original design — measured, 0 differing lines.
* Findings are reduced by delta debugging to a minimal schedule file that
  replays exactly, and reproducibility is demonstrated (the same finding
  re-shrinks to the same 343 hart-cycles in the same 20 runs).
* The tool found a real, previously unknown race in boot code that every SMP
  test in the repository links, in 4 runs of a 1,404-cycle workload.

**Refuse:**

* "First fuzzer to target a coherent multicore." Retracted; TSOtool and McVerSi
  are prior art and it is not close.
* "DiffTest does not do multicore." False.
* "The oracle problem is unsolved." Lockstep and litmus tests exist; our claim is
  about layers, not absence.
* Any performance comparison against a tool we have not run.
* Calling any liveness finding an RTL bug before it is root-caused. One of the
  two findings so far is a **software** race in `crt.S`; the other is
  unattributed. Getting this wrong once would cost the paper its credibility.
