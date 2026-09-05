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
