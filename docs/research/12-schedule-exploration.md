# Schedule exploration for RTL multicore simulation

2026-09-05. Constraints: real novelty (not patchwork), simulation-based,
**very few RTL changes**, implementable + testable + comparable in ~2 weeks.

---

## 1. The observation

**Cycle-accurate RTL simulation of a multicore is deterministic. It explores
exactly one interleaving per test program — no matter how many times you run
it.**

Silicon does not have this problem. Timing jitter, interrupts, DRAM refresh and
frequency drift naturally produce a different interleaving on every run, which
is *why* post-silicon multicore validation works at all — TSOtool relies on it.
RTL simulation removes that source of coverage precisely where bugs are
cheapest to fix: pre-silicon.

So a multicore RTL regression suite has **interleaving coverage ≈ 1 per test**,
and running it a thousand times does not improve that number.

## 2. The evidence is our own, and it is unusually direct

| observation | what it shows |
|---|---|
| Five directed reproducers for the dual-Unique bug (`mt-fencei` ×3, `mt-llist`, `mt-crosscall`) **passed on both the buggy and the fixed RTL** | The programs were not wrong. Each ran one schedule, and that schedule did not contain the race. |
| The bug's only known activation was a **871M-cycle Linux boot** | A long run accidentally samples many timings. That is coverage by brute force. |
| **Measured today** (`06`, addendum): with `bc4a4ab` reverted, all five workloads produced **bit-identical tag-change counts** to the fixed model (4091, 4158, 26090, 160148, 139716) | Removing a real coherence bug changed *nothing observable*, because the schedule never varied. This is the determinism problem, measured. |

That third row is the strongest single argument in this document, and it was
produced as a by-product of a failed experiment.

## 3. The proposal

Add a **schedule controller** to the simulation: perturb hardware timing in a
principled way so that one test program samples *many* interleavings, and do it
with a coverage notion and a probabilistic guarantee borrowed from software
concurrency testing (CHESS, and PCT's bounded priority-change scheme).

**RTL change: one input per core.** A `stall` port that gates a single ready
signal (decode/issue or commit). It does not touch coherence, the ROB, the
caches, or any pipeline logic, and **tied low it is bit-identical to today's
RTL** — so the Linux-booting design is not put at risk. Optional second-stage
knobs (snoop-response delay, arbiter priority) are equally local if the core
stall alone proves too coarse.

**Everything else is host-side C++**: the scheduler that decides *when to stall
whom*, the interleaving-coverage metric, and the corpus evaluation.

### The property that makes it sound

**Delay injection cannot create a bug that the hardware could not produce on its
own.** A core is always permitted to stall — for a cache miss, an arbiter loss,
a refill. So every schedule the controller produces is a schedule the design
could legitimately reach, and **any failure it finds is a real failure.**

This is a real distinction from fault injection, which can manufacture
impossible states and therefore needs triage. Here there are **no false
positives by construction** — worth stating as a property, not a footnote.

## 4. Why this is a new axis, not a re-run of prior work

| prior work | varies | domain |
|---|---|---|
| TSOtool (ISCA'04), McVerSi (HPCA'16), MTraceCheck | the **program** | silicon / full-system sim, where timing already varies |
| DifuzzRTL, TheHuzz, Cascade, ProcessorFuzz | the **program** | single-core RTL |
| CHESS (OSDI'08), PCT (ASPLOS'10), SegFuzz | the **schedule** | **software** |
| **this** | the **schedule** | **multicore RTL** |

Program generation and schedule exploration are **orthogonal axes**. In
hardware, only the first has been explored. The second was never needed on
silicon (timing varies naturally) and was never available in simulation
(everything is deterministic) — which is exactly why the gap exists.

## 5. Evaluation — the whole point, and it fits in two weeks

Everything needed already exists: the corpus reverts work (`bc4a4ab` reverts
cleanly), the micros exist, and the oracle layers are built (`swmr_probe`, quad
lockstep, `mt-litmus`).

**Headline metric: bugs found per simulated cycle.**

| condition | dual-Unique (CO-1) |
|---|---|
| directed test, deterministic sim | **never** — measured, bit-identical |
| Linux boot, deterministic sim | found at ~8.7×10⁸ cycles |
| directed test + schedule exploration | **?** — this is the experiment |

If a 10⁵–10⁶-cycle `mt-fencei` under schedule exploration finds what a
8.7×10⁸-cycle Linux boot found, that is a **10³–10⁴× improvement on a real
bug**, not a synthetic one. Repeat across the corpus.

Secondary: interleaving-coverage growth vs. schedules explored; sensitivity to
the number of perturbation points (PCT's bug-depth parameter).

## 6. Reviewer objections, pre-answered

**"Testbenches already randomise delays — this is standard UVM practice."**
True, and it must be cited rather than ignored. Two differences. (i) Industrial
delay randomisation is *ad hoc*: no coverage notion, no guarantee, and no way to
say how much of the schedule space was sampled. (ii) It randomises **interface**
transactions; this controls the **cores**, which is where speculation ×
coherence races are decided. Be honest that the technique is imported from
software rather than invented — the contribution is the transfer, the soundness
property, and the measured result on real bugs.

**"TSOtool/McVerSi already fuzz multicores."**
They vary the *program*, and they run where timing already varies. Orthogonal
axis; cite them as complementary, never as competitors we beat.

**"Isn't this just running longer?"**
That is the experiment, and the metric answers it: bugs per *simulated cycle*.
The Linux boot found CO-1 by brute force at 8.7×10⁸ cycles. The claim is that
directed schedule diversity beats runtime — and if it does not, we report that.

**"Does perturbation invalidate the results?"**
No — §3, soundness. Stalling is always architecturally legal, so no schedule is
manufactured that the design could not itself produce.

**"DiffTest already does multicore."**
Yes. Unrelated to this claim — that is an oracle, this is a *stimulus/schedule*
technique. Do not repeat the mistake corrected in `11`.

## 7. Risks

- **The perturbation may be too coarse.** Stalling whole cores may not produce
  the specific cache-line-level race CO-1 needs. Mitigation: add snoop-response
  and arbiter-priority knobs, still local.
- **PCT's guarantee may not transfer cleanly.** It assumes a thread scheduler
  with discrete pre-emption points; hardware interleaves at cycle granularity,
  so "bug depth" needs redefining for this domain. This is a genuine research
  question, and also the most interesting part.
- **It may find nothing.** Then the honest result is that determinism is not the
  binding constraint — publishable as a negative, but a weaker paper.
- **Incrementality.** A reviewer may read it as "PCT applied to hardware". The
  counter is the measured determinism problem (§2), the soundness property, and
  bugs-per-cycle on a real corpus — not a claim of new theory.

## 8. Why it fits the constraints

| constraint | fit |
|---|---|
| real novelty, not patchwork | new axis for hardware verification, with a theory to import and a soundness property |
| simulation-based | entirely |
| very few RTL changes | one input port per core gating one ready signal; tied low = bit-identical to today |
| ~2 weeks to implement, test, compare | corpus, oracles, reverts and micros all already exist |
| does not endanger the Linux-booting RTL | perturbation is off by default |

---

## 9. Implementation status (2026-09-05)

The tool exists and runs. It is called **Kairos** and lives in `sim/kairos/`
(design, soundness argument and porting guide: `sim/kairos/README.md`).

### RTL delta — 4 lines of logic plus plumbing

| File | Change |
|---|---|
| `common/configuration.scala` | `enableScheduleControl` knob |
| `core.scala` | `scheduleStall` input ANDed into the fetch→decode handshake |
| `testbench/chironCore.scala` | 4-bit mask fanned out to the four cores |
| `testbench/system.scala` | mask exposed at the top level |

Nothing else in the design is touched. The stall gates a handshake the design
already de-asserts on every I-cache miss, so holding it high is behaviourally
identical to fetch not being ready — it can only *delay* a hart.

### Software — header-only core, one binding per design

```
sim/kairos/dut.h          portable DUT boundary (implement this to port)
sim/kairos/dut_chiron.h   Chiron binding: stall, retire, L1 tag decode, SWMR
sim/kairos/policy.h       deterministic / random / PCT / windowed + factory
sim/kairos/coverage.h     schedule digest + order-pair coverage
sim/kairos/oracle.h       4 oracle layers, stall-aware liveness
sim/kairos/schedule.h     schedules as files: record, save, load, replay
sim/kairos/shrink.h       delta debugging over stall spans
sim/kairos/campaign.h     run_once — the only place a DUT is ever stepped
sim/kairos/main_kairos.cpp  CLI: run / sweep / replay / shrink
sim/kairos/tests/         self-tests (no RTL, ~1s, mutation-tested)
tools/kairos/analyze.py     JSON reports -> figures + findings table
tools/kairos/equiv_check.sh netlist-identity gate
mk/kairos.mk              make kairos / kairos-gate / kairos-smoke / kairos-sweep
```

### Three design decisions worth defending in the paper

**Calibration, not assumption.** A policy has to know *when* to perturb.
Spreading delays over the cycle *budget* is wrong whenever the budget is a
safety cap rather than a prediction, and it fails silently: the first working
build placed one delay window uniformly over a 400,000-cycle budget for a
6,629-cycle workload, so in ~98% of runs the window landed after the program had
already ended. Six runs, six identical digests, and a campaign that looked
perfectly healthy while exploring nothing. Kairos now measures the unperturbed
run length first and spreads over that. The same run doubles as the **control**:
if the workload does not pass unperturbed, the campaign refuses to start, because
nothing found afterwards could be attributed to scheduling.

**Liveness must be stall-aware.** A hart Kairos is deliberately holding is
*supposed* to stop retiring. The hang detector therefore counts only *unstalled*
cycles. Without that the tool reports its own perturbation as a hang and the
campaign drowns in false alarms — the single fastest way to make a fuzzer get
switched off.

**Timeout is not a finding.** A run that neither completed nor tripped an
oracle just ran out of budget. Kairos reports it as `timeout` and excludes it
from the finding count. Conflating the two would let the tool claim bugs it
never demonstrated.

### First measurement (mt-spinwait, quad-core)

Baseline: 6,629 cycles, 3,687 instructions retired, unperturbed.

| Policy | runs | distinct interleavings | novelty |
|---|---|---|---|
| `deterministic` | 6 | **1** | 0.167 |
| `windowed:2000` | 6 | **6** | 1.000 |

That 1-vs-6 is the paper's opening claim, measured rather than asserted: a
conventional RTL regression run six times explores one interleaving.

Throughput: ~36.5K cycles/s on the fast (no-trace, threaded) model with the
per-cycle four-way tag scan running, against ~42K cycles/s for the same model
with no instrumentation — the cost of continuous coherence checking is ~13%.

### The netlist-identity gate — PASSED, and it took four tries

`tools/kairos/equiv_check.sh` elaborates the pre-Kairos design in a throwaway
git worktree at HEAD, elaborates the working tree with
`enableScheduleControl = false`, and compares. Final result:

```
raw differing lines ................ 21216   (source-location comments included)
differing lines, comments stripped ....... 0
PASS — netlist identical
```

The 21,216 raw differences are Chisel's `// @[core.scala N:M]` provenance
comments, shifted because the file gained lines. Comments are not hardware. The
script reports *both* numbers on every run so the normalisation cannot hide
anything.

Getting to zero was not free, and the reason is worth a paragraph in the paper
because it is a real hazard for anyone adding an optional hook to a Chisel
design: **Chisel derives Verilog identifiers from elaboration order and from the
vals a node is bound to, so logically-neutral refactors are not netlist-neutral.**
Measured, on this one four-line hook:

| Formulation | Differing lines (comments stripped) |
|---|---|
| `getOrElse(false.B)` — the literal is still a node, shifting every later `_T_n` | 24 |
| a shared `val handshake` — binds a named wire, collapses two elaborations into one | 16,228 |
| a wrapping `match` — FIRRTL names the shared subexpression instead of repeating it | 20 |
| **`if (scheduleStall.isEmpty)` outside the statement, disabled path written as the literal original** | **0** |

Every one of those four is the same logic. Only the last one lets us say the
disabled build is the original design rather than "equivalent to" it. The
duplication in `core.scala` is deliberate and commented as such.

### The compiled-in-but-idle gate — PASSED

`make kairos-gate` runs every SMP micro under the `deterministic` policy (stall
mask held at zero) and requires each to pass exactly as it does under the
ordinary regression:

```
PASS mt-llist      PASS mt-seqlock    PASS mt-spinwait   PASS mt-fencei
PASS mt-crosscall  PASS mt-illegal    PASS mt-icoh-cross PASS mt-icoh-self
kairos-gate: hook is inert on all SMP micros
```

Together with the netlist gate above, both halves of the promise to the design
are now measured rather than asserted: **compiled out, the Verilog is the
original design; compiled in and idle, the behaviour is the original design.**

### The seven false positives, and what they cost

The first four-policy sweep reported **seven livelock findings under `pct:3`,
all of them false.** The hart dump gave it away immediately:

```
seed 1  livelock  cyc 662901
   hart 0  pc 0x00000000  retired 0
   hart 1  pc 0x00000000  retired 0
   hart 2  pc 0x80000100  retired 331211
   hart 3  pc 0x8000007c  retired 32
```

Two harts retired **zero instructions in 662,901 cycles**. That is not a design
livelock; it is Kairos holding them for the entire run. Two separate defects,
both now fixed, and both worth stating in the paper because they are the traps
anyone building this tool will hit:

**1. PCT starves spinning harts (policy defect).** Software PCT runs only the
highest-priority *enabled* thread, and the runtime knows when a thread blocks.
Hardware has no such signal. The first adaptation treated "has not retired" as
blocked, which handles a *wedged* leader but not a *spinning* one — a hart in a
spin loop retires happily forever, never yields, and the peers it is waiting for
stay stalled. Fixed by giving leadership a bounded **tenure** (`kMaxTenure`,
4096 cycles), after which the incumbent is demoted like at a change point. This
is a **deviation from PCT**, it adds priority changes the guarantee does not
account for, and it is not optional: without it the policy cannot run a
barrier-synchronised workload at all.

**2. The livelock oracle was not stall-aware (oracle defect, the serious one).**
The hang layer had always discounted deliberately-stalled cycles. The livelock
layer did not — so a policy that holds a hart for most of the run makes "the
workload never finished" trivially true, and the oracle reports the tool's own
perturbation as a property of the design. Fixed with the same rule the hang
layer uses: **a livelock verdict requires that no hart has been held for more
than 1/8 of the elapsed run.** Past that the run is reported as a plain
`timeout`, which is not a finding, so nothing is claimed.

The second defect matters beyond this tool. The soundness argument (a stall can
only produce states the design could reach unaided) constrains what *states* are
reachable; it says nothing about what an *oracle* may infer from a run in which
Kairos was itself the reason nothing happened. Liveness oracles under
perturbation need the discount applied at every layer, not just the one where it
was obviously needed.

### A third instance of the same mistake, and a limitation it exposes

Fixing the oracle stopped the false claims but `pct` still never completed the
workload, with one or two harts doing all the work. Same bug class, one level
down and this time in the policy: **a hart Kairos stalls cannot retire, so the
"has it made progress" test marked it blocked, which excluded it from the leader
set, from where it could never retire to prove otherwise.** A trap door. Fixed
by freezing a stalled hart's idle clock — the identical discount the oracle
applies, for the identical reason. Three bugs in one session, all of the form
*"we forgot to subtract our own perturbation before drawing a conclusion"*. That
is the single most important design rule for a tool of this kind and it belongs
in the paper as such.

After the fix all four harts retire (42K–84K each in one run) and `pct` explores
normally. It still does not complete `mt-spinwait`, and the per-hart dump says
why:

```
hart 0  pc 0x800002ac  retired 42133     <- barrier spin
hart 1  pc 0x80000104  retired 84276     <- startup release-flag spin
hart 2  pc 0x80000104  retired 83648
hart 3  pc 0x80000100  retired 83703
```

`0x800000f8`–`0x80000104` is the startup park loop (`lw a2,0(a1); bnez a2`,
flag at `0x80000b78`); hart 0 is already *past* the `sw zero,0(a1)` that clears
it. So three harts are loading that flag hundreds of thousands of times and
still reading non-zero. That looks like a store-visibility failure of the same
family as the barrier finding — **observed, and deliberately not claimed**: see
the limitation below.

**Limitation: `pct` is liveness-blind under the conservative oracle rule.** PCT
stalls all but one hart, so on four harts every hart is held far more than 1/8
of any run, and the livelock layer can never fire under it by construction.
`pct` can therefore only contribute *structural* and *wrong-result* findings, not
liveness ones. That is the price of a rule chosen to never cry wolf, it is a
real gap, and the honest fix is not to loosen the rule but to strengthen the
structural layer — which is where a visibility failure like the one above should
be caught anyway, at the transaction rather than at the symptom.

### Comparison sweep — 4 policies x 3 workloads x 8 runs (96 runs, ~18 min)

Workloads chosen by measured baseline so 96 runs fit a bounded budget
(`mt-seqlock` >2M cycles and `mt-llist` at 1.07M are too slow for a sweep this
shape). The livelock factor sets the per-run cycle budget, so it is
per-workload: at the default 100x a 390K-cycle workload would give every run a
39M-cycle budget.

Distinct interleavings out of 8 runs:

| Workload | baseline | `deterministic` | `random:0.02` | `pct:3` | `windowed:2000` |
|---|---:|---:|---:|---:|---:|
| mt-illegal | 1,404 | **1** | 1 | 7 | 4 |
| mt-spinwait | 6,629 | **1** | 3 | 8 | 6 |
| mt-crosscall | 390,493 | **1** | 8 | 8 | 5 |

96 runs, **1 finding** — the barrier livelock, re-found and re-shrunk to exactly
the same 343 hart-cycles in exactly 20 simulation runs, which is the
reproducibility claim demonstrated rather than asserted.

Three things this says:

* **The deterministic column is the paper's thesis in one number.** Eight runs of
  a conventional RTL regression explore one interleaving, on every workload.
* **`random` is a genuinely weak strawman on short workloads** (1/8 on
  mt-illegal, 3/8 on mt-spinwait) and only competitive on the long one, where a
  2%-per-cycle coin flip has 390K cycles to land somewhere useful. Per-cycle
  randomisation does not target anything.
* **`pct` explores best and finds least.** It leads on distinct interleavings
  everywhere, but its stall pattern means the livelock layer cannot fire under
  it (see the limitation above) — and its order-pair count is *lower* than the
  others on mt-crosscall (48 vs 144), because serialising the harts produces
  fewer distinct cross-hart orderings even while producing more distinct
  schedules. Distinct-schedule count alone would have hidden that; this is
  exactly why the coverage model reports both.

Figures generated from these JSON reports by `tools/kairos/analyze.py` into
`paper/dac27/figures/`: `fig_exploration`, `fig_novelty`, `fig_shrink`,
`table_findings.tex`.

### Both findings resolved — and both are in the harness, not the RTL

Kairos has produced two findings. **Both are software races in this
repository's own test infrastructure, and both are now fixed and A/B-confirmed.
Neither is an RTL bug.** Stating that plainly matters more than a bigger
finding count: a fuzzing paper that mis-attributes its findings loses on the
first reviewer who checks one.

| # | Workload | Reduced to | Root cause | Status |
|---|---|---|---|---|
| 1 | `mt-spinwait` | hart 2 held **343** cycles at 1383 | every hart zeroes the shared barrier state, unsynchronised, at thread entry | fixed, A/B-confirmed |
| 2 | `mt-illegal` | hart 2 held **220** cycles at 284 | every hart re-arms `hart_init_sync` before hart 0 releases it | fixed, A/B-confirmed |

Both were found in **fewer than 10 runs of a workload shorter than 7,000
cycles**, both reduce to a single sub-microsecond delay of one named hart, and
neither has ever been observed in years of a green deterministic regression —
because a deterministic simulator produces exactly the one interleaving in which
the racing stores happen to land in a benign order.

#### Finding 2 — a latent race in the project's own boot code

Found on `mt-illegal` (baseline 1,404 cycles) under `windowed:400`, shrunk to
**hart 2 held for 220 cycles at cycle 284**. All four harts spin forever in the
crt.S startup loop. Unlike Finding 1 this one is fully attributed, and it is
**not an RTL bug** — it is a race in `workloads/benchmarks/common/crt.S`:

```asm
li   x6,1
sb   x6,0(x12)        # EVERY hart sets hart_init_sync = 1
fence
bnez a0,sync_all_harts
...                   # hart 0 alone clears .bss
sw   x0,0(a1)         # hart 0 sets hart_init_sync = 0 to release everyone
sync_all_harts:
lw   a2,0(a1)
bnez a2,sync_all_harts
```

Nothing orders a slow hart's `sb 1` against hart 0's `sw 0`. If any hart is
delayed past hart 0's release, its store re-arms the flag and every hart waits
forever. The register dump is conclusive: `a0 = 0,1,2,3` (hart ids correct),
`a1 = 0x80000960` (the flag), and `a2 = 1` **on all four harts including hart
0**, the very hart that executed the clearing store.

This is a good result, not a disappointing one, and the paper should present it
as such:

* It is a **real defect**, in code every SMP test in this repository links, that
  has survived years of a deterministic regression suite running green — because
  a deterministic simulator always produces the one interleaving in which hart 0's
  `.bss` loop happens to take long enough.
* It was found in **4 runs of a 1,404-cycle workload**, and reduced to a single
  220-cycle delay of one named hart.
* It demonstrates the attribution discipline the tool needs: Kairos reports
  *"this schedule wedges the machine"*, and triage decides whether the fault is
  in the hardware or the software. Conflating the two is how a fuzzing paper
  gets its findings dismissed.

#### Finding 1 — three compounding bugs around the barrier

The mt-spinwait wedge survived the crt.S fix (all four harts still parked in the
barrier's sense-flag spin at the shifted address), which proved the two findings
were independent. Its own cause turned out to be three defects stacked:

1. **The linker script does not cover `.sbss`.** `test.ld` places only
   `*(.bss .bss.*)` and `*(COMMON)` between `_sbss` and `_ebss`. RISC-V puts
   small zero-initialised statics in `.sbss`, so `count` (0xbd0) and `sense`
   (0xbd4) — the shared barrier state in `common/util.h` — sat *at and past*
   `_ebss = 0xbd0`. **crt.S's clear loop never zeroed them.**
2. **The harness compensated with an unsynchronised store.** Every hart calls
   `initialize_count_asm(0)` as the first statement of `thread_entry`. Nothing
   orders that against a peer already inside `barrier()`, so a late hart's
   zeroing wipes a peer's `amoadd` increment. Only three of four increments
   survive, no hart ever observes the "last arriver" value, and all four spin on
   the sense flag forever — exactly the observed state (`a0 = 3`, `a3 = 1`,
   `a5 = 0` on every hart, all in their *first* barrier).
3. **That store is `sd` on an `int *`.** It writes **eight** bytes at `&count`,
   so it also clobbers the adjacent `sense` at `+4`. One racing hart destroys
   both halves of the barrier state at once.

Fixes: add `*(.sbss .sbss.* .scommon)` to the `.bss` output section (which moves
`count`/`sense` to 0xbc0/0xbc4, inside `_sbss..\_ebss`, so crt.S zeroes them),
and delete the per-hart `initialize_count_asm(0)`. Ten SMP workloads call it;
only `mt-spinwait` has been converted and A/B-tested so far.

**This is what the tool is for.** A 343-cycle delay of one hart, found in eight
runs, exposed a barrier whose shared state was never initialised by the startup
code and was being "initialised" by a race instead — in code that ten
multi-threaded tests depend on for their correctness claims.

**The earlier correction to Finding 1 stands.** With a proven startup race in the
shared boot code, "the harness raced" is now the leading hypothesis for any
liveness finding here, and the mt-spinwait barrier wedge must be re-examined
rather than left standing as a suspected AMO defect. What is currently known:
the four harts in Finding 1 are all *past* startup, parked in the barrier spin,
which the crt.S race alone does not explain (a hart caught by that race never
leaves `0x800000f8`). So the two are distinct failures. But Finding 1 stays
explicitly **unattributed** until it is root-caused, and this project's own
history says why that matters: `mt-llist` spent weeks as a suspected coherence
bug before turning out to be its own CAS loop fencing itself into a livelock.

### Testing the tool itself

`make kairos-test`: 22 cases over policies, coverage, oracle layers, schedule
round-trip/replay and the shrinker, against a mock DUT, linking no Verilated
model. It is **mutation-tested** — nine deliberate regressions are each caught:

| Mutation | Caught |
|---|---|
| remove the livelock stall discount (the seven-false-positives bug) | yes |
| remove the hang stall discount | yes (after a fix — see below) |
| fold `Retire` events into the coverage digest | yes |
| fold the cycle number into the coverage digest | yes |
| remove PCT's bounded leadership tenure | yes |
| let PCT's idle clock run while a hart is stalled | yes |
| make the windowed policy non-contiguous | yes |
| skip the shrinker's narrowing pass | yes |
| ignore the shrinker's trial budget | yes |

The second row is the point of doing this at all: the first version of the suite
**survived** that mutation, because every liveness test held its stall mask
constant and the discounted line only matters at the moment a stall is
*released*. A passing suite proved nothing there until the mutation exposed it.
Mutation testing is cheap here (recompile + 1s) and should gate any new oracle
rule.

### Open items

* Root-cause the barrier AMO finding (below).
* The CO-1 experiment: does schedule exploration surface in ~10⁵–10⁶ cycles
  what took 8.7×10⁸ to surface by accident?
* A full `make kairos-sweep` (4 policies x 8 micros) for the comparison figures.

### First finding, and what the shrinker did with it

`mt-spinwait-q4`, 8 schedules under `windowed:2000`, ~2 minutes of wall time:

```
baseline (unperturbed): ok after 6629 cycles, 3687 instructions retired
  seed 3      livelock      cyc 662901   digest d12919a3f1cc8b22 NEW
  --- windowed:2000  6/8 distinct schedules, 32 order pairs, novelty 0.750
```

`kairos shrink` then reduced it, in **20 simulation runs**:

```
shrinking: 1 span(s), 1181 hart-cycles held
reduced:   1 -> 1 span(s), 1181 -> 343 hart-cycles
  hart 2 held for 343 cycle(s) at 1383
  schedule: build/kairos-spinwait-3.ksched
```

Delaying hart 2 for 343 cycles early in the run leaves a workload that finishes
in 6,629 cycles unperturbed still running at 100x that length.

### Triaging it — and what triage needed that the tool did not have

The first attempt was a brute-force 80,000,000-cycle run (12,000x the
unperturbed length). It came back `timeout` with 79,998,284 instructions
retired, which settles nothing: `mt-spinwait` caps its own spin loops at
2,000,000 iterations, so "still running" is consistent with both a real livelock
and a merely slow schedule. **Forty minutes of wall time for zero bits of
information**, because Kairos could observe how MUCH the machine did and not
WHERE it was.

Two small additions fixed that permanently, and both belong in any tool of this
kind:

* `--console`, which echoes the guest's UART. Workloads say what happened;
  inferring it from cycle counts is guesswork.
* a per-hart state dump (last committed PC, retired count, and with
  `--dump-regs` the full GPR file) printed for any verdict that is not `ok`.

The reduced 343-cycle schedule then answered it in **16 seconds**:

```
hart 0  pc 0x800002a8   hart 1  pc 0x800002a8
hart 2  pc 0x800002a8   hart 3  pc 0x800002a8
  a0 0000...0003   a3 0000...0001   a5 0000...0000
```

0x800002a8 is the spin of the sense-reversing barrier in
`workloads/benchmarks/common/util.h` (`atomic_fetch_add(&count, 1)` ->
`amoadd.w.aqrl`):

```asm
8000028c:  amoadd.w.aqrl a4,a3,(a4)     # a4 = old count, at 0x80000be0
80000294:  addiw a0,a0,-1               # a0 = ncores-1 = 3
80000298:  beq   a0,a4, 0x800002b8      # "I am last": reset count, publish sense
8000029c:  lw    a3,0(tp)               # else spin on the sense flag ...
800002a8:  lw    a5,0(a4)               # ... at 0x80000be4
800002ac:  bne   a5,a3, 0x800002a8
```

All four harts are past the `amoadd` and all four are in the wait, each holding
`a0 = 3` (correct hart count) and `a3 = 1` against `a5 = 0`. Every hart is in
its **first** barrier — the local sense is 1 for all of them — so this is not a
fast hart lapping a slow one. **Four `amoadd.w.aqrl` increments from zero
returned a set of old values containing no 3.** That cannot happen if the four
AMOs are atomic and each executes exactly once, so the candidates are a lost
update, a duplicated (speculatively re-executed and committed) AMO, or a
visibility failure on the sense flag — the last being the least likely, since
nobody reached the publishing branch at all.

This barrier is `common/util.h`'s, shared by **every** SMP micro in the suite, and
the suite passes unperturbed (`make ci-smp`, 8/8). So this is not a systematic
AMO defect; it is a **schedule-sensitive** one, invisible to a deterministic
regression no matter how many times it runs — which is precisely the class of
bug this work exists to reach.

Status: **found, minimised, and reproducible**; not yet root-caused. Root-causing
it is a debugging task, not a tool task, and the tool has done its job by
handing over a 5-line schedule file that reproduces it on demand:

```
# kairos schedule v1
# image bins/mt-spinwait-q4.bin
1383 1726 0x4
```
