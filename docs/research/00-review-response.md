# ASP-DAC 2027 Paper 147 — reviewer complaints, mapped to current status

Submission: *"Chiron: A Composable Out-of-Order RISC-V Multicore Processor with
Cycle-Accurate Lockstep Co-Verification."* Scores **-2 / -2 / 0**. Rejected
(935 submissions, 276 accepted, 29.5%).

The paper was submitted 2026-07-01. Roughly two months of work landed after
that, so a third of the criticism is now factually out of date. This file
separates the complaints that are *already answered by the repository* from the
ones that are *real and unanswered*. Do not re-litigate the first group; do not
hand-wave the second.

---

## Group A — already fixed in the repo, needs only re-measurement + rewriting

| # | Reviewer complaint | Current status | Evidence |
|---|---|---|---|
| A1 | R1: *"The Linux claim appears overstated because µCLinux boot is reported only on the golden-model emulator. Clarify whether the RTL itself boots µCLinux."* | **Fixed.** Quad-core nommu Linux boots on the **RTL** to an interactive shell; `nproc` returns 4, typed live at the console. | `docs/linux-quad-boot.log`; memory `dual-unique-line-coherence-violation` |
| A2 | R1: *"The 10.4% branch accuracy for matmul conflicts with the 43–60% range stated in the text."* | **Both numbers were symptoms of a real bug**, not a typo. `branchRes` trained BTB/TAGE/CFI with the *next* branch's PC. Fixed: accuracy is now **85–100%** across the sweep, and Linux-boot BPred went 7% → 99%. | commit `5423298`; `README.md` "Quad-core aggregate IPC" |
| A3 | R3: *"Single-core performance is low with IPC = 0.117–0.290 compared with BOOM or SonicBOOM."* | **Improved ~2.4×**, to **0.293–0.692** single-core and **1.37–2.63** quad aggregate. Still below BOOM — see B4; this is mitigated, not answered. | `README.md`; 46-run sweep |
| A4 | R1/R2: *"Section 4 states architectural states are compared after commits from hart 0. Explain how errors in harts 1–3 are detected and localized."* | **The tool already exists and was not described.** `make lockstep-q4` (`sim/harness/lockstep_quad.cpp`) compares **all four harts'** PC + register file against the golden model every commit, and reports `racy=` for commits it must skip as legitimately racy. Measured `racy=0` on `vvadd-s1-q4` over 57,549 commits. | `VERIFICATION.md`; memory `verification-infrastructure` |
| A5 | R2: *"The reported results also show inconsistencies, including URAM utilization."* | Reporting defect. Re-run synthesis and report one consistent utilization table. | — |

**Conclusion for Group A:** these cost us three reviewers' confidence but are
now cheap to fix. They are *writing and measurement* tasks, not research tasks.
They will not, on their own, get the paper accepted anywhere.

---

## Group B — real, unanswered, and fatal if not addressed

### B1 — Novelty of the verification flow (R1, R2). **The paper-killer.**

> R1: *"The novelty of the lockstep co-verification flow is not positioned
> accurately against prior work. XiangShan DiffTest already performs
> commit-level differential verification on multicore OoO processors (MICRO
> 2022), and Dromajo provides RISC-V RTL co-simulation including BOOM (MICRO
> 2021). The authors should clarify what is actually novel in Chiron's
> verification flow."*

This is correct and cannot be argued away. Commit-level differential
co-simulation against a golden ISA model is a solved, deployed, published
technique. **"We built a lockstep checker" is not a contribution in 2026.**

Any resubmission must either (a) drop the verification-novelty claim entirely,
or (b) make a claim about something DiffTest/Dromajo demonstrably *cannot* do.
See `01-novelty.md` — option (b) is available and is well supported by our own
bug history.

### B2 — Composability is asserted, never demonstrated (R1, R3)

> R1: *"Composability is a central claim, but it is only described at the
> interface level. The paper should demonstrate an actual modification, such as
> changing pipeline depth or replacing a major module without modifying
> neighboring modules."*
>
> R3: *"There is no evaluation of the performance cost by the composable
> interfaces. They do provide benefits of flexibility, but ready/fire also
> introduce unnecessary latencies."*

Both are fair. We claim modules are swappable and never swap one in the paper,
and we never pay for the claim in cycles. R3's point is the sharper one: a
uniform externally-driven handshake at *every* module boundary is not free, and
we have never measured it. We now know from the interconnect work that
handshake turnaround is a first-order cost — the CCU was **4 cycles/beat**
purely from relay-style handshaking, and removing dead handshake states was
worth **2.35× on the Linux boot** (memory: `ccu-response-fsm-serialization`,
`ccu-deadstate-removal-2026-08`). That is direct evidence *for* R3's criticism
and, handled honestly, becomes a result rather than a wound.

### B3 — Evaluation is too narrow (R2, R3)

> R2: *"The evaluation is limited to five simple kernels and lacks meaningful
> processor or verification baselines and standard multicore workloads."*
>
> R3: *"For an EDA conference, the paper does not offer discussions on design
> flow, verification, or DSE."*

Five hand-written kernels (vvadd, matmul, filter, csaxpy, histo) is not an
evaluation. There is no standard workload, no competing processor, and no
competing verification tool anywhere in the results.

### B4 — No positioning against other open multicore processors (R1, R2)

> R1: *"The near-4× scaling is measured only against Chiron's own low-IPC
> single-core baseline. Some positioning against other open multicore
> processors is needed to establish the significance of this result."*

Speedup-over-self is the weakest possible scaling claim: a slower core scales
more easily. This must be answered with numbers from at least one other open
core, on the same workload, or the scaling result means nothing.

### B5 — The coherent-load recovery mechanism is unproven (R2)

> R2: *"The coherence-triggered load recovery mechanism is potentially
> interesting, but its correctness under multicore corner cases is not
> adequately demonstrated."*

Note the wording: **"potentially interesting"** is the only positive novelty
signal in three reviews. A reviewer pointed at the one mechanism they thought
might be a contribution and said we failed to demonstrate it. That is a
roadmap, not just a complaint.

---

## What the reviews collectively say

R2's closing line is the honest summary:

> *"Overall, the work demonstrates engineering effort but does not yet establish
> sufficient novelty."*

The engineering is not in question — R3 explicitly commended it and scored it
borderline. The paper failed because it presented a **system** where the venue
required a **result**. Fixing Group A and rewriting will not change that. The
resubmission needs a research question that Chiron is uniquely able to answer.
That analysis is in `01-novelty.md`.
