# CHIRON-BUGS — candidate inventory (draft 1)

Reconstructed 2026-09-05 from the project's bug notes and `git log`. **Nothing
here is a result yet.** This is the candidate list that Phase 1 must turn into
reproducible corpus entries, and the "predicted oracle" column is a *hypothesis
to be measured*, not a finding. If the measurement disagrees with the
prediction, the measurement wins and the paper says so.

Legend for **Class**:
**RS** = reallocated-slot (speculation) · **CO** = coherence-window
(SWMR / data-value violation) · **LV** = liveness (deadlock, livelock,
starvation, silent drop).

Legend for **Predicted catcher** — which oracle *should* fire first:
**L0** commit-level architectural lockstep (the DiffTest-equivalent baseline) ·
**L1** shadow coherence directory (SWMR + data-value invariant) ·
**L2** slot-ownership tags at ROB/PRF write ports ·
**L3** RVWMO litmus conformance ·
**W** wedge/liveness watchdog.

---

## A. Reallocated-slot class (speculation)

The shape: an operation is squashed by a mispredict, its completion survives in
a buffer, and by the time it lands the ROB has rolled back and **reallocated**
the slot. `rob.scala` writes the ready bit at `execPorts(i).robAddr` with no
ownership check; the PRF write port has no squash check either. **This shape
recurred four independent times** — which is the strongest single argument in
the paper that structural checking beats case-by-case debugging.

| id | bug | subsystem | repro | manifestation | predicted |
|---|---|---|---|---|---|
| RS-1 | ACE `responseBuffer` squash clobber — `regRecordUpdate` last-connect resurrects a squashed load's `branch.valid` on MSHR pop; stale load writes a reallocated PRF | `Dcache/ACEUnit.scala` | `mt-ipitmr` | ROB wedge at cyc 9,373,036, bit-reproducible | **L2** |
| RS-2 | Speculative MMIO never squashed — `peripheralUnit` had zero branch ageing; its MSHR came from the branch-*unaware* `fifoBaseModule` and FIRRTL pruned the `branchOps` port off the instance | `Dcache/peripheralUnit` | — (needs authoring) | silent PRF write from a squashed MMIO load | **L2** |
| RS-3 | Multiply-pipeline squash mis-pairing — 4 stages, 3 mis-zipped squash entries; `extnMPartialServicing.valid` never squashed | M extension | — (needs authoring) | stale multiply completion | **L2** |
| RS-4 | Divider `branchMask` provenance clobber | divider | `mt-ipimux` | ROB-full wedge at 27,115,312 | **L2** |
| RS-5 | Scheduler `releasedBuffer` mask bug — unguarded `branchMask` XOR kills an old divide via a recycled bit | scheduler | `mt-divburst` | `divuw` parks forever | **L2 / W** |
| RS-6 | `fifoWithBranchOps` write-cycle clobber — the branch-update loop includes the slot being written that cycle; last-connect overrides the fresh write | `Dcache/fifo.scala` | Linux boot | committed store dropped inside the D-cache | **L2 / L1** |
| RS-7 | `robFifo` out-of-window rollback | `Backend/Rob` | — | — | **L2** |

**Note:** RS-2/3/6 were fixed inside bundled commits (`1c00664`, `727dadb`), so
they will need **hand-authored minimal reverts**, not `git revert`. Budget for
that — it is the main cost driver in Phase 1.

---

## B. Coherence-window class

These are the entries that motivate oracle Layer 1, and several are *literal*
violations of the two textbook coherence invariants.

| id | bug | violates | repro | manifestation | predicted |
|---|---|---|---|---|---|
| CO-1 | **Dual-Unique line** — snoops answered out of a `fence.i` walker writeback hand a peer pre-store data *with PassDirty*, so two harts hold the line Unique at once (measured: `V1 M1 S0` in both L1s) | **SWMR** | Linux boot | `/init` hang; violation at ~cyc 871,230,872, hang observed much later | **L1** |
| CO-2 | **Stale `CleanUnique` upgrade** — an in-flight CleanUnique completing after invalidation re-validates the dead way from stale BRAM, erasing a peer's committed store | **DVI** | `mt-lrscirq` | livelock; fatal window 3,799,375–3,799,685 | **L1** |
| CO-3 | **Word-granular snoop kill** — reservation kill compared word/dword-granular but snoop addresses are line-aligned, so only dword-0 reservations were ever killed; SC succeeded without owning the line | **DVI** / RVWMO | `mt-lrsc` phase 1 | silent lost update → kernel corruption | **L1 / L3** |
| CO-4 | CCU `FSM_12` responder select picked by core index, so a clean sharer could outrank the dirty owner → PassDirty dropped → nobody writes back → L2 stale | **DVI** | `mt-lrscirq` | stale fill from ancient L2 | **L1** |
| CO-5 | `CleanUnique` on a valid **clean** copy returned no data (condition was `dirty && !shared`), destroying the system's last valid copy | **DVI** | `mt-lrscirq` | stale fill | **L1** |
| CO-6 | **L2 MSHR write-miss refill corruption** — the written slice was patched with the *live* `W_data` wire (another request's bytes ~100 cycles later) instead of the stored `deq.bits.data`; poison installed dirty, later evicted to DRAM | **DVI** | Linux boot | spinlock line filled with kernel-text bytes at cyc 62,894,813 | **L1** |
| CO-7 | **Read-miss overtakes its own writeback** — same-line refetch passes the un-drained writeback through the ring; L2 returns the pre-eviction version, so two partial versions of the line exist | **DVI** | Linux boot | wedge #4, ~cyc 59.4M | **L1** |
| CO-8 | I-cache/D-cache `fence.i` coherence — clean-on-fence walker did not write dirty lines back to L2 | **DVI** | `rv64ui-p-fence_i` | ISA test failure (83/84) | **L0** ✓ |

CO-8 is the control: it is the one coherence bug in this list the **existing**
architectural oracle *did* catch, because it happened to be single-core and
short. Keep it in the corpus for exactly that reason.

---

## C. Liveness class

**The commit-level oracle is structurally blind to every entry in this table** —
a machine that stops committing produces nothing to compare. This is the
sharpest part of the argument and needs no new mechanism to demonstrate, only
measurement.

| id | bug | subsystem | repro | manifestation | predicted |
|---|---|---|---|---|---|
| LV-1 | **Arbiter atomic-lockout deadlock** — the post-read-pass atomic window blocked an older speculative load *and* unrelated snoops → CCU-wide freeze | `Dcache/arbiter.scala` | Linux boot | all four harts' memory traffic dead at ~cyc 65.5M | **W** |
| LV-2 | **LR/SC forward-progress livelock** — each LR takes the line exclusive and the snoop kills peers' reservations before their SC lands; 4 harts made *zero* progress over 130M+ cycles | `cacheLookupUnit` | `mt-lrsc` phase 1 | livelock (constrained-loop guarantee violated) | **W** |
| LV-3 | CCU snoop starvation + ROB coherent-squash livelock | CCU / ROB | Linux boot | SMP boot hang | **W** |
| LV-4 | Per-core request starvation in the D-cache coherency path | `Dcache` | `csaxpy-q4`, `histo-q4` | quad benchmarks hang | **W** |
| LV-5 | **D-cache `reqSched` silent drop** — `requestScheduler` discards a request landing on a full queue; the dropped op's ROB entry wedges the hart | `Dcache` | Linux boot | silent hart wedge | **W** |
| LV-6 | CCU/L2 cold-I-fetch deadlock after a D-cache miss | CCU / L2 | `csaxpy-s2..s5` | hang | **W** |
| LV-7 | Divider `mExtensionReady` last-connect drops a released divide | core / M ext | `mt-divburst` | Linux RTL wedge | **W** |
| LV-8 | M-extension shared response port — divide and multiply share `extnMResponse`; the divide block is textually last and silently drops a landing multiply | M ext | `make solid` | wedge | **W** |
| LV-9 | **Illegal-instruction trap absent** — `execPorts.exceptionOccurred` hardwired false, so an illegal instruction wedged the ROB head silently and forever | rob / core / decode | `mt-illegal` | silent infinite hang (pre-fix RTL **hangs**, does not fail) | **W** |
| LV-10 | Guard v1/v2 deadlocks — the LR/SC reservation guard's own fixes deadlocked twice before v3 | `cacheLookupUnit` | `mt-lrsc` phase 3 | victim hart parked at `lr.d.aq` | **W** |

LV-10 is worth keeping as its own entry: **two successive fixes for a livelock
each introduced a new deadlock.** That is a strong argument that this bug class
is not addressable by careful review, which is the paper's thesis in miniature.

---

## D. Negative controls — keep these, they are evidence

A corpus of bugs is only half the argument. These are documented cases where the
existing gates gave the *wrong* answer, and they belong in the paper because
they quantify the oracle's blind spot directly.

| id | case | why it matters |
|---|---|---|
| NC-1 | A branch-window guard **passed `riscv-tests` 84/84** while deadlocking all five quad benchmarks at exactly 500,000 cycles | the architectural oracle passed a design that could not run |
| NC-2 | The `injFSM` escape fix **passed ISA 84/84, ci-bench 5/5, and two open reproducers** — and corrupted memory, killing the Linux boot | *"a green suite does not validate a speculation-path change"* |
| NC-3 | For CO-1, **five directed reproducers passed on both the buggy and the fixed RTL** (`mt-fencei` plain / release-dirtied / `DIRTY_LINES=512`, `mt-llist`, `mt-crosscall`) | directed testing did not discriminate at all; only a state dump did |
| NC-4 | `mt-llist` "hung" — but it was the **test's own CAS loop** fencing itself into a livelock, not an RTL bug | false positives are part of the honest cost of liveness checking |
| NC-5 | `msip` set-wins CLINT fix and the `robFifo emptyReg` rollback fix were both real hardening but **provably not** the bug being chased (0 `msip` writes at the wedge; bit-identical trace) | the corpus must distinguish *a fix* from *the fix* |

NC-4 and NC-5 keep the paper honest and pre-empt the obvious reviewer question
("how many of your alarms are false?").

---

## Risks, stated up front

1. **Bundled fixes.** `727dadb` and `1c00664` each contain several independent
   fixes. Minimal reverts must be hand-authored and individually validated.
2. **Reproducibility drift.** Several bugs were found on a much older tree; the
   surrounding code has since changed (source layout, interconnect rewrite,
   128-bit ACE). Some will not reproduce on today's `main` and must be recorded
   as *historical, not reproducible* — honestly, in the paper.
3. **Duplicate root causes.** Some entries above will collapse into one defect.
   Expect attrition from ~25 candidates to perhaps 12–15 corpus entries.
4. **Long runtimes.** CO-1 activates at ~871M cycles and CO-6 at ~63M. Even at
   40K cycles/s that is hours per run. **Every corpus entry needs a minimized
   reproducer** — this is a Phase 1 work item, not a detail. Checkpoint/restore
   (`ckpt/`) exists and should be used.

## Immediate next actions (no long runs required)

- [ ] Resolve each candidate to a **fix commit** and a **file+line**, and mark
      which ones are bundled.
- [ ] Decide the corpus record format and create `bugs/` with two worked
      examples end to end (propose **RS-1**, which has a cycle-exact trace and a
      committed fix, and **CO-1**, which is the SWMR showpiece).
- [ ] Prototype the Layer-1 shadow directory against a *fixed* build first, to
      establish it produces **zero** false SWMR/DVI alarms on a known-good model
      before it is ever pointed at a bug.
