# Verification

What is actually verified in this repository, how to reproduce it, and — just
as importantly — what is *not* covered. If you are evaluating chiron, reviewing
it, or picking it up as a maintainer, read the [Known gaps](#known-gaps)
section as carefully as the results.

Chiron is a quad-core RV64IMA out-of-order processor written in Chisel. The
design is verified by co-simulation against a golden functional emulator plus a
layer of per-cycle microarchitectural assertions; there is no formal proof, and
none of what follows should be read as one.

---

## Toolchain

These are the exact versions the results below were produced with. Two of them
are hard constraints, not preferences.

| Component | Version | Notes |
|---|---|---|
| Verilator | **4.038** (2020-07-11) | **Pinned.** Verilator 5.x renames internal signals (e.g. `system__DOT__memory__DOT__memory`) that `sim/rtl/rtl_model.h` and every probe reach into by name. 5.x builds, then fails to find the nets. CI pins `ubuntu-22.04` because its apt Verilator is 4.038. |
| Chisel | 3.5.4 | `build.sbt` |
| Scala | 2.13.8 | `build.sbt` |
| sbt | 1.12.4 | `project/build.properties` |
| JDK | 17 | sbt requires 17+; Ubuntu 22.04 defaults to 11, so CI installs Temurin 17 explicitly |
| riscv64-unknown-elf-gcc | 13.2.0 | Only needed to *rebuild* workloads. The 84 committed ISA images and 57 committed workload binaries mean the ISA, benchmark and assertion gates all run without it. |
| g++ | 11.4.0 | host harnesses |
| Python | 3.10.12 | `tools/gen_stress.py` |

`make sim` regenerates Verilog from Chisel and Verilates it. Note the build
trap documented in `mk/rtl.mk`: the recipe is `(sbt "runMain system"; mv ...)`,
and the `;` swallows sbt's exit status, so a *failed* elaboration can be
followed by copying a stale `system.v`. If you change RTL, confirm the change
reached the Verilog before trusting a result:

```sh
rm -f system.v sim/rtl/system.v && make sim-fast
grep -c '<a net your change introduces>' sim/rtl/system.v   # must be > 0
```

Beware that a purely combinational `val` may be folded away by FIRRTL and leave
no net of that name even when the change *is* present — pick a marker that
survives (a register, or a named condition), or verify behaviourally instead.

---

## The verification layers

Ordered by strength of oracle, weakest first. Each answers a different
question, and the later ones exist because the earlier ones missed real bugs.

### 1. ISA suite — `make isa`

84 riscv-tests images run in lock-step against the golden emulator
(`sim/emulator/`), comparing architectural state after every committed
instruction. **Result: 84/84.**

**What it does not prove.** It is single-core and short-running. A branch-window
guard once passed 84/84 while deadlocking all five multi-core benchmarks at
exactly 500,000 cycles. *ISA alone is not a sufficient gate for any change to
the speculation or squash paths.*

### 2. Quad-core benchmarks — `make ci-bench`

Five multi-threaded benchmarks at max scale (vvadd, matmul, filter, csaxpy,
histo) must each reach `BENCHMARK COMPLETE`. **Result: 5/5 PASS.**

**What it does not prove.** It asks "did it produce the right answer?", not "did
the microarchitecture stay self-consistent?". Speculation bugs here corrupt
state silently in places that often do not happen to change the result.

### 3. Microarchitectural assertions — `make ci-check`

The same five benchmarks, run through a harness with
`sim/harness/invariants.h` compiled in (`-DCHIRON_INVARIANTS`). Host-side
observers read Verilated state every cycle and never drive it, so enabling them
cannot change RTL behaviour. Per hart, per cycle:

| Assertion | Violated when |
|---|---|
| `BRANCH-READY-NO-RESOLVE` | A conditional branch's ROB ready bit is set by something other than its own resolution — i.e. an execution port completed under a `robAddr` whose slot has since been reallocated. Attributes the culprit port (ALU / M-ext / load). |
| `READY-OUTSIDE-ROB-WINDOW` | Any port readies a ROB slot that is not currently allocated. The same defect one step earlier, and not branch-specific. |
| `DOUBLE-RESOLVE` | One branch resolved twice with no allocation between. |
| `WEDGED` | No retirement for `CHIRON_WEDGE_CYCLES`; prints the decoded jammed ROB head so a hang self-diagnoses. |

A violation fails the run even if the benchmark computed the right answer.
**Result: 5/5 CLEAN, zero violations.**

### 4. Constrained-random speculation stress — `make stress-sweep`

`tools/gen_stress.py` emits seeded RV64IMA programs weighted toward the corners
the directed benchmarks never reach: divides issued into branch shadows,
back-to-back divides, nested unpredictable branches that exhaust
`branchMaskWidth = 4`, speculative MMIO reads, cross-hart AMO, LR/SC retry
loops, `fence.i`. Branch outcomes are data-dependent so the predictor cannot
learn them.

The programs are deliberately **not** self-checking — the oracle is external
(lock-step, or the assertions above). Everything derives from `SEED`, which is
baked into the generated file, so a failing seed replays with one command.

**Result: 8 seeds × 300 blocks, all CLEAN.**

```sh
make stress-sweep STRESS_SEEDS="1 2 3 4 5 6 7 8" BLOCKS=300
make stress-bin SEED=<n> BLOCKS=300 && make stress-run SEED=<n>   # replay one
```

Requires the RISC-V toolchain (it compiles generated C), so unlike the layers
above it does **not** run in CI as configured — see [CI](#continuous-integration).

### 5. Linux SMP boot

A single-node nommu Linux 6.3 SMP image boots on the RTL. See
[Current status](#current-status) — this is the one layer that is not green.

---

## Are the assertions actually any good?

An assertion that never fires is unfalsifiable, so layer 3 was validated in
**both** directions.

**False positives.** Zero violations across `ci-check` (5 benchmarks) and
`stress-sweep` (8 seeds × 300 blocks) on the fixed RTL. This matters as much as
detection: a checker that cries wolf gets switched off, and then protects
nothing.

**True positives.** A `git worktree` was built with *only* the divider
`branchMask` ageing fix reverted, giving it its own `obj_dir_fast` so it could
not disturb the main model. On that known-bad RTL, `mt-ipimux` raises
`READY-OUTSIDE-ROB-WINDOW` **233 times, first at cycle 35,797**, naming
instruction `0x02faf7b3` (`remu a5,s5,a5`, the divide at `0x80000548`) and
attributing it to the M-extension port on all four harts.

The same defect, hunted by hand, does not manifest as a visible wedge until
cycle **26,814,085**. The assertion sees it roughly **750× earlier** and names
the instruction and the port, instead of leaving a stalled pipeline to be
reverse-engineered.

Worth noting which assertion fires: `READY-OUTSIDE-ROB-WINDOW`, not the
branch-specific one. Catching the bad write *before* the reallocated slot
happens to be occupied by a branch is what buys the head start.

---

## The defect taxonomy

Four separate wedges in this design turned out to be one bug shape, which is
the single most useful thing to know when working on it:

> **A completion lands on a ROB/PRF slot that speculation has already rolled
> back and reallocated.**

`rob.scala:120-126` writes the ready bit at `execPorts(i).robAddr` with no
ownership check, and the PRF write port has no squash check, so the completion
silently readies or overwrites whatever now occupies the slot. When the new
occupant is a branch, it retires before its own resolution arrives, the late
resolution flushes, and the ROB jams at a head that can never complete.

| # | Instance | Cause |
|---|---|---|
| 1 | ACE `responseBuffer` clobber | `regRecordUpdate` last-connect resurrected a squashed load's `branch.valid` |
| 2 | Speculative MMIO never squashed | `peripheralUnit` had no branch ageing at all; its MSHR came from a branch-unaware base class and FIRRTL pruned the port |
| 3 | Multiply pipeline squash mis-pairing | 4-stage pipeline, 3 mis-zipped squash entries; one stage never squashed |
| 4 | Divider `branchMask` provenance clobber | In-flight divide aged against a *waiting* instruction's mask, stripping its speculation bits until it became unsquashable |

Every one was a squash/ageing guard keyed off the wrong stage or the wrong
condition. Write these as an explicit `(sourceMask, sourceValid, destValid)`
list with source and destination adjacent; parallel `Seq(...).zip(Seq(...))`
idioms hide exactly this mis-pairing.

The structural fix — an ownership/generation tag checked at the ROB write ports
— would have made all four non-events and is not yet implemented.

**Do not** implement it as "suppress resolutions for retired entries". That was
tried: it passed ISA 84/84 and deadlocked all five benchmarks at exactly
500,000 cycles.

---

## Continuous integration

`.github/workflows/generic_test.yaml` runs on every push, pinned to
`ubuntu-22.04` for the Verilator 4.038 constraint above. It builds the RTL from
Chisel and runs, as hard gates:

1. `make runLockStep` — single lock-step smoke test
2. `make test_all_images` — the 84-image ISA suite
3. `make ci-bench` — the five quad-core benchmarks
4. `make ci-check` — the same five under per-cycle assertions

CI does **not** install the RISC-V toolchain: it runs entirely on committed
images — the 84 ISA tests in `sim/tests/riscv-isa/images/` and the 57 workload
binaries in `bins/`. That keeps runs fast, at the cost of two things being
outside CI:

* `make stress-sweep`, which compiles generated C and therefore needs
  `riscv64-unknown-elf-gcc`;
* rebuilding any workload, so a stale committed `.bin` would not be detected.
  (Related trap: `bins/*.bin` are tracked, so a `git checkout` that deletes or
  reverts them can produce false passes. Rebuild them if benchmark results move
  unexpectedly.)

To add the stress sweep to CI, install the toolchain in the workflow and append
`make stress-sweep STRESS_SEEDS="1 2 3 4" BLOCKS=200`. Keep the seed list short
— each seed is a full RTL simulation.

---

## Current status

| Layer | Status |
|---|---|
| ISA suite (84) | **84/84** |
| Quad-core benchmarks (5) | **5/5 PASS** |
| Assertions on those 5 | **5/5 CLEAN** |
| Random stress (8 seeds × 300 blocks) | **all CLEAN** |
| Assertion true-positive | **proven** (233 detections on known-bad RTL, 750× earlier than the manual find) |
| Linux SMP boot | **boots to `Run /init as init process`, then hangs** |

### The open Linux failure

The kernel reaches `/init` at ~763M cycles and the console then goes silent.
At 1.589 billion cycles: hart 1 sits in `smp_call_function_many_cond+0x2e4`
(`csd_lock_wait`, awaiting a cross-call) while harts 0/2/3 cycle through the
idle loop. **Nothing is wedged** — every hart advances and the assertions report
zero violations — so this is *not* the reallocated-slot class above.

Ruled out by fast bare-metal repros (~1M cycles instead of 1.6B):

* `mt-ipiwfi` **PASSES** — IPIs are delivered even to a hart idling on `wfi`.
  (Found along the way: the RTL implements no WFI at all. `decode.scala` handles
  only `funct12` `0x302`/`0x000`/`0x800`, so `0x105` falls through and retires
  as a NOP. That is spec-legal, but the path had zero coverage — no other
  workload in the tree executes a `wfi`. The *emulator* does implement WFI, and
  `lockstep_linux.cpp` calls `clear_wfi()` purely to hide the divergence.)
* `mt-csdwait` **PASSES** — a spin-on-load loop containing `cpu_relax()` (which
  on RISC-V is literally `div x,x,zero`) does observe a peer's store, so this is
  not a stale-load or coherence fault.

Both negative, both narrowing: delivery and visibility are fine, so the fault is
upstream — the target hart never runs the callback.
`sim/harness/probes/linux_ipi_probe.cpp` boots the real image counting CLINT
msip writes per target hart plus each hart's `mstatus.MIE` / `mie.MSIE` /
`mip.MSIP`, which separates "the kernel never raised the IPI" from "it raised it
and the hart would not take it". PC sampling cannot.

---

## Known gaps

Stated plainly, because they bound what the results above mean.

* **No quad-core lock-step.** Lock-step against the golden model is single-core
  only. SMP divergence is caught structurally (assertions) or by a benchmark
  producing a wrong answer — not architecturally at the first wrong register.
  This is the largest single gap.
* **No formal verification.** No model checking, no equivalence checking, no
  proofs. Everything here is simulation.
* **Coverage is not measured.** There is no functional-coverage instrumentation,
  so "the stress generator reaches those corners" is an argument from
  construction and from the bugs it exercises, not a measured claim.
* **The assertions cover the ROB completion path only.** They say nothing about
  the cache coherence protocol, the AXI/ACE interfaces, or the L2 — areas that
  have historically had bugs.
* **WFI is not implemented in RTL** (see above). Legal, but it means the idle
  path diverges from the emulator and lock-step is deliberately blinded there.
* **Single Linux image.** One kernel configuration, one device tree. No
  userspace test suite runs on the target.
* **Performance numbers are simulation-only.** No synthesis timing closure, area
  or power figures are claimed here; see `fpga/` for the Kintex-7 flow.

---

## Reproducing

```sh
# Build the RTL (Chisel -> Verilog -> Verilator). ~20 min.
make sim          # traced model, used by the ISA lock-step suite
make sim-fast     # no-trace -O3 model, used by benchmarks and Linux

# The gates, cheapest first
make isa          # 84 ISA tests in lock-step vs the golden model
make ci-bench     # 5 quad-core benchmarks
make ci-check     # the same 5, with per-cycle assertions
make stress-sweep # seeded random speculation stress (needs riscv-gcc)

# Linux
make linux-sim    # boot the SMP image on the RTL, live console
```

Gates report by exit code, not by grepping their own output:
`0` clean, `2` timeout, `3` deadlock, `4` invariant violation.

Debug probes for the ROB/speculation paths live in `sim/harness/probes/` and
each carries a header comment explaining the failure it was written for and how
to read its output. Start with `robhead_probe` for a jammed ROB.
