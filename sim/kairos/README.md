# Kairos — schedule exploration for multicore RTL simulation

> *kairos* (καιρός): the opportune moment. The bug is already in your RTL; what
> you are missing is the instant at which it becomes visible.

A cycle-accurate RTL simulation of a multicore is **deterministic**. Given a
test program it executes exactly one interleaving of that program, and it
executes that same interleaving every time — a thousand runs of your SMP
regression explore one schedule, not a thousand.

Silicon has no such limitation. Clock jitter, interrupt arrival, DRAM refresh,
voltage droop and thermal throttling re-order the machine on every execution,
which is why post-silicon validation keeps finding coherence and memory-ordering
bugs that pre-silicon simulation ran past. That variation is a *coverage
source*, and simulation throws it away precisely at the stage where bugs are
cheapest to fix.

Kairos puts it back. It **delays harts** according to a controlled, reproducible
policy, so a single test program samples many interleavings. It then decides
whether each interleaving was legal, reduces any failure to the few delays that
caused it, and reports the result as a file you can replay.

---

## Why the failures are real

The single design decision everything rests on:

> **Kairos may only delay a hart. It may never make one go faster, skip work,
> win an arbitration it would have lost, or change any value.**

On Chiron this is one signal, `scheduleStall`, gating the fetch→decode
handshake. Holding it high is behaviourally identical to fetch simply not being
ready — a condition the design already produces for itself on every I-cache
miss. So:

* Every schedule Kairos induces is a schedule **the unmodified design could
  reach on its own.** A failure under Kairos is a failure of the design, not an
  artefact of the tool. There is no false-positive class to triage.
* The tool cannot mask a bug either: with the mask held at zero the design
  behaves exactly as it does without Kairos. `make kairos-gate` measures that on
  every SMP test — currently 8/8 PASS.
* When the feature is compiled out (`enableScheduleControl = false` in
  `common/configuration.scala`) the generated Verilog is identical to a build
  that never heard of Kairos — **measured**, not asserted:
  `tools/kairos/equiv_check.sh` elaborates the pre-Kairos design in a throwaway
  git worktree at HEAD and diffs. Zero differing lines once Chisel's
  `// @[file line:col]` provenance comments are stripped; the script prints the
  raw count too, so the normalisation cannot hide anything.

That is a property, not a hope, and it is what separates this from fault
injection: injecting a fault produces failures the silicon would never have; a
delay produces failures the silicon eventually will.

---

## What it does with a schedule

Four layers of oracle run on every cycle of every run. They answer *different*
questions, and each is reported separately so you know which one fired.

| Layer | Question | Catches what nothing else does |
|---|---|---|
| 1 **Result** | did the workload compute the right answer? | — (every regression has this) |
| 2a **Hang** | has a hart stopped retiring entirely? | wedged ROBs, lost wakeups, deadlocked arbiters |
| 2b **Livelock** | everyone retires, nobody finishes? | spin loops nobody releases — invisible to every value-comparison oracle |
| 3 **Structural** | is the machine in a state it cannot legally be in? | fires at the *transaction*, not at the eventual symptom ~10⁸ cycles later |
| 4 **Architectural** | did committed state diverge from a golden model? | the strongest oracle, when a reference model exists |

Both liveness layers **discount cycles in which Kairos was holding the hart**.
For 2a that is obvious. For 2b it is easy to forget and it cost seven false
positives here: a policy that holds a hart for most of a run makes "the workload
never finished" trivially true, so a livelock verdict additionally requires that
no hart has been held for more than 1/8 of the elapsed run. The soundness
argument constrains which *states* a stall can produce; it says nothing about
what an *oracle* may infer from a run Kairos itself stalled into silence.

Layer 2b is measured **relative to the unperturbed run**: the workload finished
in *B* cycles with no stalls, and it has now run more than *K·B* without
finishing. Kairos knows exactly how many hart-cycles it held, so a *bounded*
slowdown is expected and an unbounded one is not. Unlike layer 3, this is a
**suspicion that needs triage**, not a proof — a test carrying its own
bounded-wait assumption can trip it legitimately — so it is reported under its
own name and never folded in with the structural violations.

Layer 3 checks SWMR (single-writer/multiple-reader) and per-line state directly
on the four L1 tag arrays, every cycle. This is the layer that matters for this
project's history: the dual-Unique coherence bug was found by hand-dumping tag
arrays and noticing that two harts held the same line Unique; it manifested as a
Linux `/init` hang roughly 10⁸ cycles later, in a different subsystem, and five
directed reproducers passed on both the buggy and the fixed RTL.

Layer 2 has one subtlety that is easy to get wrong and fatal if you do: **a hart
Kairos is deliberately stalling is supposed to stop retiring.** The liveness
detector therefore counts only *unstalled* cycles. Without that, the tool
reports its own perturbation as a hang and the campaign drowns in false alarms.

---

## From "seed 41 fails" to a bug report

A campaign that finds a bug hands you a schedule holding harts thousands of
times across millions of cycles, essentially none of which matters. Kairos
reduces it.

* The failing run is **re-recorded** as a list of stall spans
  (`start end mask`), which is a plain-text file.
* **Delta debugging** (Zeller & Hildebrandt, TSE 2002) removes spans until no
  single remaining span can be dropped without losing the failure.
* A second pass **binary-searches each surviving span's width** from both ends.

The predicate is *"does this still reproduce the **same** verdict"*, not merely
*"does this still fail"* — reducing a hang into some easier wrong-result failure
would silently retarget the reduction onto a different bug.

Delta debugging normally struggles with concurrency because the predicate is
flaky. Here it is not: the DUT is a cycle-accurate simulator and the candidate
schedule is fully specified, so the predicate is a *pure function* — each
candidate is tested once and the answer never changes. The same determinism that
makes RTL simulation blind to interleavings is what makes reduction over them
exact.

The output looks like:

```
  reduced: 1174 -> 2 span(s), 88213 -> 47 hart-cycles, 96 simulation run(s)
    hart 2 held for 41 cycle(s) at 812004
    hart 0 held for  6 cycle(s) at 812061
    schedule: build/kairos-windowed-41.ksched
```

That is a sentence an architect can act on, and the `.ksched` file can be
checked into the repository as a regression, attached to a bug report, diffed,
or hand-edited to test a hypothesis.

---

## Policies

A policy decides, each cycle, which harts to hold. Every policy is a pure
function of `(seed, cycle, observed progress)` — no wall-clock, no I/O — which
is why a finding is reproducible from its seed alone.

| Policy | What it does | Why it is here |
|---|---|---|
| `deterministic` | never stalls | the **control**: what your regression does today, and the proof that a deterministic simulator explores one interleaving |
| `random[:p]` | each hart held with probability *p* per cycle | the honest strawman — roughly what a UVM testbench does with randomised delays. If it matches PCT, PCT is not earning its complexity |
| `pct[:d]` | random hart priorities plus *d−1* random priority-change points, with bounded leadership tenure | PCT (Burckhardt et al., ASPLOS 2010) transferred to hardware |
| `windowed[:w]` | one hart held for one contiguous window | wide races (a walker writeback outliving a peer's refill) need a *contiguous* delay; per-cycle coin flips reopen and reclose them immediately |

**PCT needs a fourth deviation that is easy to miss: bounded tenure.** Software
PCT runs only the highest-priority *enabled* thread and the runtime knows when
one blocks. Hardware has no blocking signal, and "has not retired" does not
substitute — a hart spinning on a flag retires forever and would hold the
machine forever, starving the peers it is waiting for. Measured, before
`kMaxTenure` existed: `pct:3` left two harts with **zero** retired instructions
after 662,901 cycles. Leadership therefore expires after 4096 cycles. This adds
priority changes the guarantee does not account for, and is stated rather than
hidden.

**On PCT's guarantee — read this before citing it.** Software PCT gives
probability ≥ 1/(n·k^(d−1)) of finding a depth-*d* bug in an *n*-thread, *k*-step
program. The *structure* transfers (it is a counting argument over priority
orderings). Three things do not transfer cleanly, and Kairos does not pretend
otherwise: in hardware every *cycle* is a scheduling point, so *k* is far larger
and the bound correspondingly weaker; hardware harts never "block" the way a
thread waiting on a lock does, so a demoted leader does not automatically hand
off; and the depth of a coherence race is not obviously the same notion as the
depth of a data race. Kairos's position is *"we transfer PCT's structure and
measure how the bound behaves in this domain"* — not *"we inherit PCT's
guarantee"*.

Adding a policy means implementing `decide()` in `policy.h` and registering it
in `make_policy()`. Nothing else in Kairos needs to know it exists.

---

## Measuring exploration honestly

Two metrics, deliberately sensitive to different things:

* **`distinct_schedules`** — count of distinct 64-bit digests over the *ordered
  sequence of cross-hart memory events*. The digest deliberately excludes
  retirements and cycle numbers: two runs that produce the same event order at
  different absolute times **are the same interleaving**, and counting them
  separately is exactly the mistake a naive "hash the whole trace" metric makes.
  It would inflate every result and make any policy look good.
* **`order_pairs`** — the set of observed *(hart A touched line L before hart
  B)* orderings. Much coarser, much harder to saturate by accident, and
  therefore the number to look at when someone asks whether "distinct schedules"
  is doing real work.
* **`novelty_rate`** — fraction of runs producing an interleaving never seen
  before. For a deterministic simulator this collapses to 1/runs, which is the
  entire motivation for this tool expressed as one number.

---

## Using it

```sh
make kairos            # build build/kairos.out
make kairos-test       # self-tests: policies, coverage, oracle, schedules, shrinker
make kairos-gate       # prove the RTL hook is inert when the mask is zero
make kairos-smoke      # 4 schedules on one SMP micro
make kairos-sweep      # every policy across every SMP micro -> build/kairos/*.json
```

```sh
# explore, and reduce anything that fails
build/kairos.out run --image bins/mt-seqlock-q4.bin --done-pc 0x80000ab4 \
    --policy windowed:4096 --runs 64 --cycles 8000000 --shrink

# compare policies under one budget
build/kairos.out sweep --image bins/mt-llist-q4.bin --done-pc 0x80000bdc \
    --policies deterministic,random:0.02,pct:3,windowed:4096 --runs 32 \
    --json build/kairos/mt-llist.json

# reproduce a finding, anywhere, later
build/kairos.out replay --image bins/mt-llist-q4.bin --done-pc 0x80000bdc \
    --schedule build/kairos-windowed-41.ksched
```

`tools/kairos/analyze.py` turns the JSON reports into the figures and tables.

**Known limitation.** `pct` stalls all but one hart, so on four harts every hart
is held far more than 1/8 of any run and the livelock layer can never fire under
it. `pct` contributes structural and wrong-result findings, not liveness ones.
That is the price of an oracle rule chosen never to cry wolf.

### Two things the tool does *not* claim

**A `timeout` is not a finding.** A run that neither completed nor tripped an
oracle simply ran out of budget; that is an under-provisioned campaign, not a
bug, and Kairos scores it separately. Conflating the two is how a fuzzer claims
bugs it never demonstrated.

**A failing baseline stops the campaign.** The calibration run doubles as the
control: if the workload does not pass unperturbed, Kairos refuses to start,
because nothing found afterwards could be attributed to scheduling.

### Testing the tool itself

Every serious bug Kairos has had was in its own reasoning rather than in the
simulator — a livelock layer that forgot to discount deliberate stalls and
reported seven bugs that were not there, a "blocked" test that starved the
machine, a coverage digest that would have counted run length as exploration.
None of those show up as a crash; a broken tool emits confident output.

`make kairos-test` asserts the properties those conclusions rest on, against a
mock DUT, in a binary that links no Verilated model. It runs in about a second,
so it gates `make kairos-gate` too. The suite is **mutation-tested**: nine
deliberate regressions (removing either stall discount, removing PCT's bounded
tenure, folding retirements or cycle numbers into the coverage digest, skipping
the shrinker's narrowing pass, ignoring its trial budget, making the windowed
policy non-contiguous) are each caught. One of those mutations survived the
first version of the suite and exposed a genuine hole — the released-stall
case — which is now covered.

**One operational warning, learned the hard way in this tree:** the harness
links `Vsystem__ALL.a` *statically*. Running `build/kairos.out` directly after
rebuilding the RTL silently executes the **previous** model, and the run looks
completely normal — same output format, plausible cycle counts. Always `make
kairos` (or check its mtime against `sim/rtl/obj_dir_fast/Vsystem__ALL.a`)
before believing a result that is supposed to reflect an RTL change.

---

## Porting Kairos to another core

Everything above `dut.h` — policies, coverage, oracle, campaign, shrinker — is
design independent. Porting means two things:

1. **One RTL signal.** Add an input that gates an existing ready/valid
   handshake, so that holding it high only delays the hart. Do not synthesise a
   new behaviour; find backpressure the design already has and add one more
   reason for it. On Chiron this is 4 lines in `core.scala` plus plumbing.

2. **One header.** Implement `kairos::Dut` (see `dut_chiron.h`, ~200 lines):
   step a cycle, apply a stall mask, report retirements, report completion.
   Declare in `capabilities()` what your design can actually observe —
   everything a DUT cannot supply is reported as unsupported rather than faked,
   because a coverage number computed from events the DUT never observed is
   worse than no number.

Coherence observation is optional. With retirement only, Kairos still explores
schedules and still runs the liveness and result oracles; the coverage model is
coarser and the report says so.

---

## Files

| File | Contents |
|---|---|
| `dut.h` | the portable DUT boundary — implement this to port |
| `dut_chiron.h` | the Chiron binding: stall signal, retirement, L1 tag decode |
| `policy.h` | scheduling policies and the factory |
| `coverage.h` | schedule digest and order-pair coverage |
| `oracle.h` | the four oracle layers and the stall-aware liveness rule |
| `schedule.h` | schedules as files; recording and replay |
| `shrink.h` | delta debugging over stall spans |
| `campaign.h` | `run_once` — the single place a DUT is ever stepped |
| `main_kairos.cpp` | CLI: `run`, `sweep`, `replay`, `shrink` |
| `tests/test_kairos.cpp` | self-tests for all of the above — no RTL, ~1s (`make kairos-test`) |

## Related work

Kairos is not the first tool to test a multicore under randomised conditions,
and the paper says so plainly. TSOtool (Hangal et al., ISCA 2004) ran
pseudo-random racy multiprocessor programs and checked them against formal TSO;
McVerSi (Elver & Nagarajan, HPCA 2016) generated memory-consistency tests with
genetic programming, explicitly prioritising non-determinism; DiffTest
(XiangShan) does commit-level differential testing including multicore. What
those share is that they vary the **program** and observe at the **architectural
result**. Kairos varies the **schedule** of a fixed program and observes at the
**microarchitectural state**, and it reduces what it finds. Those are
complementary axes: commit-level differential testing is oracle layer 1 here,
and layers 2 and 3 exist because a hang and an illegal cache state never show up
as a non-racy commit mismatch at all.
