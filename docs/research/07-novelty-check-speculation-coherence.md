# Deep novelty check: speculation × coherence — result

Done 2026-09-05, before committing weeks of work. **Outcome: most of the pitch
does not survive, and the part that does rests on one unverified observation
with a known confound.** This is the check doing its job — it cost a session
and would have cost weeks.

---

## 1. KILLED — selective coherent-load replay

The proposal was: replace Chiron's full-ROB flush on coherent load invalidation
with selective replay.

**[Cain & Lipasti, "Memory Ordering: A Value-Based Approach", ISCA 2004]**
(https://pharm.ece.wisc.edu/papers/isca2004cain.pdf) already does exactly this.
On a snoop invalidation the LSQ is probed and speculative loads squashed;
value-based replay is reported to eliminate **95% of consistency squashes on
average in multiprocessor configurations**, replacing the pipeline flush with
selective load re-execution. [InvisiFence, ISCA 2009]
(https://dl.acm.org/doi/10.1145/1555815.1555785) is adjacent follow-on work.

Also worth knowing: **the full flush Chiron does is the textbook mechanism, not
an oddity.** Intel's memory-ordering machine clear asserts a "nuke" that clears
all unretired instructions on a snoop hit in the load buffer, patented since the
P6 era. So there is no "Chiron does something unusually crude" angle either.

**Do not propose selective replay as a contribution.** It is 22 years old and
the prior result is strong.

## 2. OCCUPIED IN AN ADJACENT CONTEXT — adaptive speculation throttling

**[US10073784B2](https://patents.google.com/patent/US10073784B2/en)** describes
speculation throttling — memory instructions forced in-program-order when
interference is detected. But the trigger is **hardware-transactional-memory
abort rate** ("a transaction has failed and needs to be executed
non-speculatively", "an excessive transaction abort rate"), and the goal is
shrinking a transaction's read/write footprint. Not coherence contention on
ordinary synchronization.

Different trigger, different goal, different context — but close enough that a
reviewer will raise it. Cite it; do not claim the idea.

## 3. ADJACENT, NOT COMPETING — LR/SC scalability

**[LRSCwait / Colibri (Riedel, Gantenbein, Ottaviano, Hoefler, Benini, 2024)]
(https://arxiv.org/abs/2401.09359)** — "LRwait/SCwait, a synchronization pair
that eliminates polling by allowing contending cores to sleep", evaluated on an
open RISC-V platform with **256 cores**. That platform (MemPool) is **in-order**,
and the paper is about polling contention, throughput and energy — 6.5× and
7.1× over LRSC-based implementations.

It does **not** touch out-of-order speculation depth. Good citation, and it
usefully shows the in-order manycore side of this space is taken by a strong
group (ETH/Benini) while the OoO side is not.

> **Correction on method:** my first read of this paper came from fetching the
> PDF, and the summary I got back asserted it evaluated out-of-order cores and
> discussed speculation depth. That was wrong — invented from a compressed PDF.
> The abstract page gave the real content. **Fetch the abstract, not the PDF.**

## 4. APPEARS UNOCCUPIED — the liveness claim

Nothing found occupies: *an out-of-order implementation's **speculation depth**
can break the RISC-V **architectural forward-progress guarantee** for
constrained LR/SC loops, and the depth at which it breaks is a design parameter
nobody reports.*

This is a **compliance/liveness** claim, not a performance one, which is what
distinguishes it from Cain & Lipasti (performance) and the HTM patent
(footprint). It is topical — the RISC-V community is actively discussing
["Multiple LR/SC forward progress guarantee levels"](https://lists.riscv.org/g/tech-privileged/topic/proposal_for_multiple_lr_sc/95620158).

It is also **much narrower than the direction as pitched.**

---

## 5. The problem: our evidence for §4 is not sound

The entire surviving claim rests on one line in `mlp-limiters-and-spec-depth-trap`:

> **But 32/bm6 LIVELOCKS mt-llist.** ISA passed 84/84; mt-llist then ran 5m35s
> against a ~20s expectation before I killed it.

Three reasons that is not yet evidence:

1. **It was never root-caused.** The run was *killed*, not diagnosed. "Ran long
   then I stopped it" is consistent with livelock, with slow-but-progressing,
   and with an unrelated stall.
2. **There is a known confound, and it is the same test.**
   `mt-llist-preexisting-wedge` records that `mt-llist` hung because of **its own
   testbench bug** — a redundant `__sync_synchronize()` next to
   `__sync_bool_compare_and_swap`, putting two pipeline-draining fences between
   the load of `head.first` and the `lr.d` that must observe it, so under
   contention the compare failed before `sc.d` was ever reached. Fixed in
   `74bf2aa`. That note's own conclusion: *"SOLVED, and it was a testbench bug.
   Supersedes both the earlier 'pre-existing wedge' and **'LR/SC forward-progress
   weakness'** framings — the RTL was never at fault."*
3. **The MLP note itself attributes the livelock to that same mechanism** —
   *"deeper speculation changes the CAS retry timing — see
   [[mt-llist-preexisting-wedge]] for how that loop starves."* A timing-sensitive
   broken CAS loop starving harder under deeper speculation is a **testbench**
   result, not an RTL forward-progress violation.

Whether the A/B was run before or after `74bf2aa` is not recorded, and that
detail decides it.

**So the honest position: we have a suggestive observation, not a finding, and
the closest prior investigation of the same symptom concluded "not the RTL".**

---

## 6. Recommendation

**Do not commit to this direction yet. Run one cheap experiment that decides it.**

Set `robAddrWidth = 5`, `branchMaskWidth = 6`, rebuild, and run `ci-smp` —
especially `mt-llist` (now with the *fixed* hand-written `cmpxchg64`) and
`mt-lrsc` phase 2, which is the control that runs the same algorithm with
different atomics. Then:

- **If a hart genuinely makes zero forward progress** under deeper speculation
  on a *correct* LR/SC loop → the claim is real, it is an architectural
  compliance violation, and the direction is alive with strong evidence.
- **If it merely runs slower, or it is the testbench again** → the direction is
  dead. Say so and pivot, rather than building on it.

Cost: one config change, one rebuild, one `ci-smp` run — under a day, no night
job. Compare against the fixed-config baseline in `mlp-limiters-and-spec-depth-trap`
(16/4 vs 32/4 vs 32/bm6 vs 16/bm6 cycle counts are already tabulated there).

**Do not skip the control.** `mt-lrsc` phase 2 running the same algorithm with
hand-written atomics is what separates "RTL cannot guarantee progress" from
"this test's CAS loop is fragile".

---

## 7. Experiment 1 result (2026-09-05) — suggestive, control still missing

Ran `ci-smp` at both configurations, same tree, rebuilt each time.

| config | `ci-smp` | `mt-llist` |
|---|---|---|
| **baseline** `robAddrWidth=4` / `branchMaskWidth=4` (ROB 16, mask 4) | **ALL PASS** | passes (~1.07M cycles) |
| **deep** `robAddrWidth=5` / `branchMaskWidth=6` (ROB 32, mask 6) | **FAILURES** | **hit the 30,000,000-cycle cap** |

Every other gated micro passed at both settings (seqlock 7.9M, spinwait 7040,
fencei 583902, crosscall 389070, illegal 1400, icoh-cross 372433, icoh-self
387557). **Only `mt-llist` fails, and only at depth.**

**This is stronger than the note it came from.** The original observation was
made when `mt-llist` still had its own testbench bug (the redundant
`__sync_synchronize()`); that was fixed in `74bf2aa` and the committed bin was
refreshed, so **this failure is on the FIXED test**. The confound identified in
§5 no longer explains it.

**But it is not settled, for two reasons:**

1. **The control was never run.** `mt-lrsc` is *not* in `CI_SMP_TESTS`, so the
   discriminating experiment — phase 2 runs the same push/drain algorithm with
   hand-written atomics — did not execute. Required next:
   `make litmus`-style run of `mt-lrsc-q4` at deep settings.
   - `mt-lrsc` also fails at depth → a general forward-progress failure, and the
     claim is real.
   - `mt-lrsc` passes → specific to `mt-llist`'s CAS loop, and the honest
     reading is a timing-fragile test rather than an architectural ceiling.
2. **"Did not finish in 30M cycles" is not yet "livelock".** `mt-llist` has its
   own no-progress watchdog that reports `stalled=1`; the gate's grep discarded
   the failure dump. Re-run capturing full output to distinguish livelock from
   28× slowdown.

If the control confirms, the claim becomes: **speculation depth in an OoO
multicore is bounded by coherence liveness — not by area or timing — and the
bound is reachable at ordinary parameter values (ROB 32 / mask 6).** That is
simulation-only, needs no FPGA, and is directly relevant to SMT, which raises
contention further.
