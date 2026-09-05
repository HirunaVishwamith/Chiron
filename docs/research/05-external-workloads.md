# External workloads — what was downloaded and what is actually usable

Fetched 2026-09-05 into **`/media/hv/D1/Projects/chiron-external/`** — outside
this repository, nothing vendored, nothing added to git.

## Download cost

| repo | network transfer | on disk | ref |
|---|---|---|---|
| `litmus-tests/litmus-tests-riscv` | 5 MB | 64 MB | shallow (`--depth 1`) |
| `SakalisC/Splash-3` | 7 MB | 32 MB | shallow |
| `embench/embench-iot` | 1 MB | 29 MB | shallow |
| `eembc/coremark` | 1 MB | 2 MB | shallow |
| **total** | **~14 MB** | 126 MB | |

The ~19 MB estimate in `02-plan.md` was based on the GitHub API `size` field,
which reports the **packed** repository. Actual transfer came in under it; the
126 MB on disk is checkout expansion, not data used. `herd/herdtools7` (60 MB +
an OCaml toolchain) was **not** downloaded and is not needed — see below.

---

## 1. Litmus tests — usable, and better equipped than expected

**9,932 `.litmus` tests**, organised as `non-mixed-size/` (BASIC_2_THREAD, CO,
ATOMICS, RELAX, SAFE, FENCE.TSO, RelAcq_2_THREAD, SINGLE_INST, …) and
`mixed-size/`.

Two result sets ship with the repo, which is what lets us skip herdtools:

- **`model-results/herd.logs`** (4.8 MB) — the axiomatic model's verdict per
  test: the full set of **allowed** final states.
- **`hw-results/SiFive-Freedom-U540.log`** — outcomes measured on **real
  hardware**. This is a bonus we did not plan for: it gives a *silicon* RISC-V
  multicore to compare Chiron's observed outcomes against, not just a model.

A test is a small two-thread program with an initial state and a final
condition. Example, `non-mixed-size/BASIC_2_THREAD/LB+ctrl+po.litmus`:

```
{ 0:x6=x; 0:x7=1; 0:x8=y;
  1:x6=y; 1:x7=1; 1:x8=x; }
 P0             | P1          ;
 lw x5,0(x6)    | lw x5,0(x6) ;
 bne x5,x0,LC00 | sw x7,0(x8) ;
 LC00:          |             ;
 sw x7,0(x8)    |             ;
exists (0:x5=1 /\ 1:x5=1)
```

and `herd.logs` gives its four allowed states, with
`Observation LB+ctrl+po Sometimes 1 3`.

**The oracle is therefore simple and needs no external tool:** run the test on
the RTL many times under timing perturbation, collect the observed final states,
and assert **observed ⊆ allowed**. Any state outside the model's allowed set is
an RVWMO violation. That is oracle Layer 3.

**Work required:** a `.litmus` → bare-metal RV64 generator (parse the init
block, emit P0/P1 onto harts 0/1 with a barrier and a result collector). This is
what `litmus7` does; we write a small version of it. `elf-tests/basic/` in the
repo is *not* that — it is only C thread-startup smoke tests.

---

## 2. Embench-IoT — the best find

**15 of 19 benchmarks are integer-only**, so they run on RV64IMA with no
floating point and no soft-float apology:

`aha-mont64` · `crc32` · `edn` · `huffbench` · `matmult-int` · `md5sum` ·
`nettle-aes` · `nettle-sha256` · `nsichneu` · `picojpeg` · `qrduino` · `slre` ·
`tarfind` · `wikisort` · `xgboost`

Excluded (contain `float`/`double`): `depthconv`, `sglib-combined`,
`statemate`, `ud`.

This is a proper standard single-core suite with published numbers for other
open cores, and it directly answers R2's *"limited to five simple kernels"*.

---

## 3. CoreMark — straightforward

Ships a **`barebones/`** port (`core_portme.c/.h/.mak`, plus `ee_printf.c`), so
it needs only a timer hook (CLINT `mtime`) and a character sink (our UART). This
produces **CoreMark/MHz**, the one number essentially every open core publishes
— which is exactly the comparison R1 asked for.

---

## 4. Splash-3 — only `radix` survives, as predicted

| directory | benchmarks | usable on RV64IMA? |
|---|---|---|
| `codes/kernels` | cholesky, fft, lu, **radix** | only **radix** |
| `codes/apps` | barnes, fmm, ocean, radiosity, raytrace, volrend, water-nsquared, water-spatial | **none** — all floating point |

`radix.c.in` does `#include <math.h>` and mentions float/double 25 times, but
that is in the timing/statistics reporting, not the sort itself; the kernel is
an integer radix sort. It is written against the SPLASH macro layer
(`MAIN_ENV`, `CREATE`, `LOCKDEC`/`LOCK`/`UNLOCK`, `BARRIER`, `CLOCK`), which
normally expands to pthreads via `codes/pthread_macros`.

**Port strategy:** substitute a bare-metal macro set that expands onto Chiron's
existing 4-hart barrier/lock primitives in `workloads/benchmarks/common`, and
strip the FP reporting. This is the most effort of the four but it is the one
that makes the multicore comparison apples-to-apples, since Culsans and
OpenPiton both report Splash-3.

---

## 5. Dhrystone

Not downloaded — check `riscv-tests/benchmarks/dhrystone` first, which the
toolchain already provides locally.

---

## Revised expectation vs `02-plan.md`

The plan assumed Splash-3 would carry the multicore comparison. It can, but
**only through one kernel**. Embench-IoT unexpectedly carries the single-core
comparison much better than planned (15 benchmarks vs the 5 hand-written
kernels), and the litmus repo's bundled hardware results add a comparison point
we had not counted on. Net: the evaluation section is in better shape than the
plan assumed, and the FP constraint bit exactly where predicted.
