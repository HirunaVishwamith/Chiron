<div align="center">

<img src="docs/chiron.png" alt="Chiron" width="720"/>

# Chiron

### A quad-core RV64IMA out-of-order processor, in Chisel

*In ancient lore **Chiron** (Kai-ron) was the wisest of the Centaurs — not a wild brute,*
*but a gentle healer and the supreme teacher of heroes: Achilles, Heracles, Jason.*
*He walks on four limbs (a **quad-core** lineage) yet his legacy is to **educate**.*
*This core is built in that spirit — a teaching-grade, fully verified OoO machine.*

<br/>

![ISA](https://img.shields.io/badge/ISA-RV64IMA-5b2c6f)
![Cores](https://img.shields.io/badge/cores-4-informational)
![Chisel](https://img.shields.io/badge/Chisel-3.5.4-d22128)
![Scala](https://img.shields.io/badge/Scala-2.13.8-dc322f)
![Verilator](https://img.shields.io/badge/verified-Verilator%20lock--step-1f6feb)
![ISA tests](https://img.shields.io/badge/riscv--tests-84%2F84-2ea44f)
![Linux](https://img.shields.io/badge/Linux-SMP%20boots%20to%20shell-2ea44f)
![Target](https://img.shields.io/badge/clock-75%20MHz-555)

</div>

---

## Highlights

- **Quad-core** RV64IMA — 4 independent OoO harts sharing a **non-blocking L2**
  and an **ACE coherent interconnect** (8 ports, 2 per core).
- **Out-of-order** pipeline per core — register renaming, a reorder buffer, a
  centralized issue queue with wake-up, and in-order commit.
- **Full memory hierarchy** — split L1 I/D caches, non-blocking L2 with MSHRs
  and pseudo-LRU, ACE-style coherent interconnect.
- **Modern branch prediction** — a **4-table TAGE** direction predictor
  (512 entries each, histories 4/9/19/40) over a 2048-entry bimodal base, plus a
  256-entry direct-mapped BTB, a 512-entry pre-decode CFI classifier and a
  16-deep RAS. TAGE drives next-PC combinationally, in the fetch cycle.
- **Cycle-accurate, lock-step verified** against a C++ golden-model emulator,
  one committed instruction at a time — **84/84 official `riscv-tests` pass**.
- **Boots quad-core Linux SMP to an interactive shell** — a nommu RISC-V kernel
  brings up all four harts, runs userspace, and answers `nproc` with `4`, typed
  live at the console (`docs/linux-quad-boot.log`).
- **One command per task.** No copying files around: every harness loads images
  by path, driven from a single benchmark manifest.
- **164 hardware performance counters** exposed from the RTL (41 per core × 4)
  — IPC, branch accuracy, cache miss rates, ROB-head stall decomposition,
  per-class latency attribution.

---

## See it run

```bash
make fire            # bare-metal Doom-fire demo, UART → terminal
make cube            # rotating wireframe cube, software 3D on four harts
```

Both render over the UART with truecolor escapes — no framebuffer, no FPU.
`workloads/demos/g3d/` is a small fixed-point 3D library (matrices, projection,
line and triangle raster) written for exactly this: the harts transform vertices
in parallel and core 0 rasterises and presents.

<div align="center">
<img src="docs/fire.gif" alt="Chiron fire demo" width="720"/>
</div>

---

## Microarchitecture

```mermaid
flowchart LR
    subgraph C0["Core 0"]
      direction TB
      FE0["Frontend\n(TAGE BPU)"] --> BE0["OoO Back-end\n(ROB · IQ · PRF)"]
      BE0 <--> MH0["L1 I/D"]
    end
    subgraph C1["Core 1"]
      direction TB
      FE1["Frontend"] --> BE1["OoO Back-end"]
      BE1 <--> MH1["L1 I/D"]
    end
    subgraph C2["Core 2"]
      direction TB
      FE2["Frontend"] --> BE2["OoO Back-end"]
      BE2 <--> MH2["L1 I/D"]
    end
    subgraph C3["Core 3"]
      direction TB
      FE3["Frontend"] --> BE3["OoO Back-end"]
      BE3 <--> MH3["L1 I/D"]
    end

    MH0 & MH1 & MH2 & MH3 --> IC["ACE Interconnect\n(8 ports)"]
    IC --> L2["L2 non-blocking · MSHR · pseudo-LRU"]
    L2 --> RAM[(Main Memory)]
```

### Per-core parameters

| Property | Value |
|---|---|
| ISA | RV64IMA (no F/D/V/C) |
| Reorder buffer | 16 entries |
| Physical registers | 64 (LVT-based rename) |
| Issue queue | 8 entries, centralized |
| Commit width | 1-wide (decode is also 1-wide) |
| Divider | Radix-4 (2 bits/cycle), clz-normalized, /0 /1 /small early-out |
| L1 I-Cache | 2-way · 64 sets · 16-instr lines |
| L1 D-Cache | 2-way · 64 sets · 8×8-byte lines |
| Branch predictor | Bimodal + BTB + 4×512 TAGE |
| Clock target | 75 MHz |

### System parameters

| Property | Value |
|---|---|
| Cores | 4 (hart IDs 0–3) |
| Coherence | ACE — 2 ports per core (8 total) |
| L2 Cache | Non-blocking · MSHR · pseudo-LRU |
| UART | MultiUart (one per core) |
| RAM base | `0x8000_0000` (sim) · `0x4000_0000` (Zynq) |

---

## Repository layout

```
chiron/
├── src/main/scala/        # Chisel RTL — grouped by pipeline function:
│   ├── Frontend/          #   fetch + branch prediction (TAGE/BTB)
│   ├── Decode/            #   decode + register rename
│   ├── Backend/           #   OoO execution: Rob · Prf · Scheduler · StoreDataIssue
│   ├── pipeline/          #   shared port/fifo bundles (pipeline.ports / .fifo)
│   ├── Icache/            #   L1 instruction cache (+ shared AXI bundle)
│   ├── Dcache/            #   L1 non-blocking data cache
│   ├── L2_cache/          #   shared non-blocking L2 (MSHR · pseudo-LRU)
│   ├── Interconnect/      #   ACE coherence unit (CCU)
│   ├── common/            #   configuration / parameters
│   ├── testbench/         #   system top, main memory model, UARTs
│   └── core.scala         #   per-core top-level (frontend → backend → L1)
├── sim/                   # all host-side C++ (golden model · RTL wrapper · drivers)
│   ├── emulator/          #   C++ golden-model ISA simulator (4-hart, lock-step ref)
│   │   ├── hart.h         #     one hart — split into hart_{csr,trap,alu,memory,execute}.inc
│   │   ├── emulator.h     #     4-hart container
│   │   └── terminal.h · clint.h · constants.h
│   ├── rtl/               #   Verilator RTL wrapper
│   │   ├── rtl_model.h    #     single-core (core 0) signal accessors + stepping
│   │   └── profiler.h · profiler_quad.h   # perf-counter read-out
│   ├── harness/           #   test/run drivers
│   │   ├── common/        #     shared helpers: args.h · image.h · completion.h
│   │   ├── lockstep.cpp   #     RTL-vs-emulator lock-step (core 0)
│   │   ├── lockstep_isa.cpp   # ISA regression completion
│   │   ├── lockstep_linux.cpp # Linux-boot variant
│   │   ├── profile.cpp    #     single-core cycle-accurate profiler
│   │   ├── profile_quad.cpp   # quad-core profiler (all 4 cores + aggregate IPC)
│   │   └── fire.cpp       #     bare-metal UART → terminal streamer
│   ├── tests/riscv-isa/   #   ISA regression images (images · avoid · dumps)
│   └── data/              #   runtime inputs: Image · qemu.dtb · boot.bin
├── workloads/
│   ├── benchmarks/        # benchmark sources (vvadd · matmul · filter · csaxpy · histo)
│   └── demos/             # bare-metal demos (fire 🔥)
├── bins/                  # staged .bin images
│   ├── mt-*-s1..s5.bin    #   single-core scaled variants
│   └── mt-*-q4.bin        #   quad-core (NUM_CORES=4) base variants
├── mk/                    # modular makefiles
│   ├── config.mk          #   paths, compiler flags, SHOW_STATE / DUMP_WAVES vars
│   ├── benchmarks.mk      #   benchmark manifest (done-PCs, families)
│   ├── rtl.mk             #   Chisel → Verilog → Verilator
│   ├── bins.mk            #   single-core .bin build + stage
│   ├── bins_quad.mk       #   quad-core .bin build + stage (NUM_CORES=4)
│   └── run.mk             #   all harness build + run targets
├── scripts/               # profiling visualisation, log decoders
├── build/                 # all generated artifacts (gitignored)
└── Makefile               # thin orchestrator — one entry point per task
```

---

## Quick start

### Prerequisites

```bash
sudo apt install verilator sbt make g++ python3
# RISC-V toolchain must be on PATH (riscv64-unknown-elf-gcc)
# If sbt hangs on file watches:
make fix-inotify
```

### Step 1 — Build the RTL

```bash
make sim        # Chisel → Verilog → Verilator library (~5 min first time)
```

> `make sim` fails if `sbt` fails. It will not copy a stale `system.v`.

### Everyday RTL gate

After any frontend/backend change:

```bash
make gate            # lockstep vvadd-s1 + vvadd-q4, compared to testdata/baseline/q4
make compare         # re-score whatever JSON is already in build/profile_results
make regress-q4      # all five q4 benches, then compare (slow)
make snapshot-baseline   # refresh testdata/baseline/q4 from the current JSONs
```

`profile_quad` now scrapes `The code is ran with error code: N` from UART
and exits 4 if N is nonzero — a done-PC hit with a wrong result array is a
fail, not a silent pass. The JSON `result.bench_error` field records it.

---

## Running in single-core mode

Single-core runs use the pre-built `bins/mt-*-s<scale>.bin` images (compiled with
`NUM_CORES=1`). The benchmark name is `<family>-s<scale>` where scale 1 is smallest.

### Lock-step verification (correctness)

Compares RTL output against the golden emulator instruction-by-instruction:

```bash
make lockstep BENCH=vvadd-s1     # smallest, fastest (~30 s)
make lockstep BENCH=csaxpy-s2    # ~5 min
make lockstep BENCH=matmul-s2    # ~8 min
```

Logs are written to `build/run.log`, `build/states.log`, `build/regs.log`.

### Cycle-accurate profiling (IPC)

```bash
make profile BENCH=vvadd-s1      # → build/profile_results/vvadd-s1.json
make profile BENCH=csaxpy-s3
```

### Profile all single-core scales (s1–s5) for all benchmarks

```bash
make profile-all-sc              # → build/profile_results/<fam>-s<N>.json + chart
```

#### Single-core benchmark profile (s1 scale)

<div align="center">
<img src="docs/profile_report.png" alt="Single-core benchmark profile report" width="720"/>
</div>

| Benchmark | Cycles | IPC | Branch Acc | D$ Miss | DRAM RD BW |
|---|---|---|---|---|---|
| filter-s1 | 1 074 831 | **0.408** | 80.3 % | 2.13 % | 2.39 MB/s |
| vvadd-s1 | 329 665 | **0.308** | 73.3 % | 1.43 % | 1.40 MB/s |
| csaxpy-s1 | 383 297 | **0.281** | 69.5 % | 1.12 % | 1.16 MB/s |
| histo-s1 | 1 872 971 | **0.262** | 67.6 % | 1.10 % | 1.21 MB/s |
| matmul-s1 | 1 114 553 | **0.335** | 27.5 % | 0.55 % | 0.40 MB/s |

> Single-core IPC now sits at 0.26–0.41, roughly 2.3× the pre-TAGE numbers, with
> branch accuracy up from ~50–60 % to 68–80 % on the streaming kernels — the
> predictor change is most of the gain. filter leads because its stencil inner
> loop is long and well-predicted; histo trails because its scatter pattern
> serialises on the store path. matmul's branch accuracy is low because the
> trip-count-3 inner loop is a hard pattern for TAGE; the IPC is still
> competitive because the inner body is mul-heavy and cache-resident.

---

## Running in quad-core mode

Quad-core runs use the `bins/mt-*-q4.bin` images (compiled with `NUM_CORES=4`).
All 4 harts execute cooperatively; the profiler reads all 164 performance counters.

### Quad-core profile for a single benchmark

```bash
make profile-quad FAM=vvadd      # → build/profile_results/vvadd-q4.json
make profile-quad FAM=csaxpy
make profile-quad FAM=matmul
make profile-quad FAM=filter
make profile-quad FAM=histo
```

### All quad-core benchmarks + chart

```bash
make profile-all                 # profiles every family, generates profile_report.png
```

### Quad-core pass/fail regression

```bash
make test-q4                     # profile-based pass/fail on vvadd-q4
```

---

## Profiling values — reference numbers

Measured on the current RTL over the full sweep (`make profile-sweep`); the
figures below are the `s1` scale, and `docs/profile_report.png` covers every
family at every scale, single-core and quad-core.

### vvadd-s1-q4 (vector-vector add, all 4 cores)

| Metric | Aggregate | Core 0 | Cores 1–3 |
|---|---|---|---|
| **IPC** | **1.279** | 0.302 | ~0.326 |
| Instructions retired | 401 962 | 94 967 | ~102 330 each |
| Max cycles | 314 347 | — | — |
| Branch accuracy | — | 76.2 % | 77.9 % |
| D-cache miss rate | — | 1.7 % | ~4.3 % |
| ROB stall % | — | 29.8 % | ~5.4 % |
| Decode efficiency | — | 66.3 % | ~94.6 % |

> Core 0 acts as the coordinator (barrier + result check), hence its lower IPC
> and high ROB stall fraction. Cores 1–3 execute the compute kernel.

### histo-s1-q4 (histogram, all 4 cores)

| Metric | Aggregate | Core 0 | Cores 1–3 |
|---|---|---|---|
| **IPC** | **1.298** | 0.263 | ~0.345 |
| Instructions retired | 2 125 836 | 430 506 | ~565 100 each |
| Max cycles | 1 637 361 | — | — |
| Branch accuracy | — | 71.8 % | 78.7 % |
| D-cache miss rate | — | 1.0 % | ~1.2 % |
| ROB stall % | — | 35.6 % | ~4.0 % |

---

## Debugging features

### Print internal state each step (`SHOW_STATE`)

Prints the golden-model register file after every committed instruction — useful
for diagnosing mismatches or tracing program flow:

```bash
make lockstep BENCH=vvadd-s1 SHOW_STATE=1
```

Works on any `lockstep` variant. Writes to stdout alongside the existing log files.

### Capture waveforms (`DUMP_WAVES`)

Writes a VCD waveform to `build/system_trace.vcd` for viewing in GTKWave:

```bash
make lockstep BENCH=vvadd-s1 DUMP_WAVES=1
# Then open:
gtkwave build/system_trace.vcd
```

> **Performance note:** VCD generation enables Verilator signal instrumentation
> (`traceEverOn`), which substantially increases simulation overhead. Use only
> when you need waveforms; omit it for routine lock-step runs.

Both flags can be combined:

```bash
make lockstep BENCH=csaxpy-s2 SHOW_STATE=1 DUMP_WAVES=1
```

---

## Full regression

```bash
make test
```

Runs in two stages:

- **ISA suite** (`make isa`) — 84 official `riscv-tests` images, lock-step RTL
  vs golden model. Progress is printed per-test. Expected result: **84/84**.
  (`rv64ui-p-fence_i` was the long-standing exception; it passes now that the
  D-cache writes dirty lines back to L2 on `fence.i`.)
- **Quad-core vvadd** (`make test-q4`) — profile-based pass/fail on
  `bins/mt-vvadd-q4.bin`.

Two further gates cover the multi-core paths that the ISA suite, being
single-hart, cannot reach:

- **`make ci-bench`** — all five quad-core benchmark families must complete
  cleanly.
- **`make smp-repro`** — bare-metal reproductions of the failures found while
  bringing up Linux SMP: an illegal-instruction trap and cross-hart code
  publication (`fence.i`), each with a control build that is *expected* to fail.

---

## Verification — lock-step

Correctness is proven by running the **RTL** and the **C++ golden model** in
lock-step, comparing architectural state after **every committed instruction**:

```mermaid
sequenceDiagram
    participant R as RTL (Verilator)
    participant G as Golden model (hart 0)
    loop per committed instruction
        R->>R: tick until core 0 commits
        G->>G: step one instruction
        R-->>G: compare 32 GPRs + CSRs + PC
        Note over R,G: mismatch → dump states.log / regs.log, exit ≠ 0
    end
```

Divergences dump `run.log`, `states.log`, `regs.log` to `build/` for debugging.

Lock-step is the strongest oracle but it is **single-core only**, and it asks
only "is the architectural state right?". Two further layers sit above it:

| Gate | Question it answers |
|---|---|
| `make ci-check` | Did the *microarchitecture* stay self-consistent? Per-cycle assertions (`sim/harness/invariants.h`) catch a completion landing on a ROB slot speculation already reallocated — the bug shape behind four separate wedges in this design. |
| `make stress-sweep` | Seeded constrained-random programs aimed at the speculation corners the directed benchmarks never reach (divides in branch shadows, speculative MMIO, cross-hart AMO/LR-SC). |

**See [VERIFICATION.md](VERIFICATION.md)** for what each layer proves, the exact
toolchain versions results were produced with, the defect taxonomy, and — read
this before trusting any of it — the known gaps.

---

## Performance counters

The RTL exposes 41 counters per core (164 total). The profiler aggregates them
into:

| Metric | Source |
|---|---|
| IPC | `inst_retired / cycles` |
| Aggregate IPC (quad) | `Σ(inst_retired) / max(cycles)` |
| Branch accuracy | `branches_passed / branch_total` |
| D-cache miss rate | `dcache_miss / dcache_reqs` |
| I-cache miss rate | `icache_miss / decode_ready` |
| Scheduler stall % | `scheduler_stalls / decode_ready` |
| ROB stall % | `rob_stalls / decode_ready` |
| Decode efficiency | `decode_fired / decode_ready` |
| DRAM read BW | `l2_to_mem_rd_beats × 8 B × 75 MHz` |

JSON reports are written to `build/profile_results/`.

---

## Timing reference (Verilator, ~6 500 RTL cycles/sec)

| Benchmark | Approx wall time |
|---|---|
| vvadd-s1 (lock-step) | ~30 s |
| vvadd-q4 (profile) | ~2 min |
| csaxpy-s2 (lock-step) | ~5 min |
| csaxpy-s5 / csaxpy-q4 | ~25 min |
| matmul / filter / histo (q4) | 5–15 min |

---

## Make target reference

| Target | What it does |
|---|---|
| `make sim` | Build the RTL (Chisel → Verilog → Verilator) |
| `make bins` | Build + stage single-core `.bin` images |
| `make bins-q4` | Build + stage quad-core `.bin` images (`-DNUM_CORES=4`) |
| `make bins-all` | Both of the above |
| `make emu BENCH=…` | Run benchmark on the golden emulator (fast, no RTL) |
| `make lockstep BENCH=…` | Lock-step RTL vs emulator; writes logs to `build/` |
| `make lockstep … SHOW_STATE=1` | Same, plus per-step golden-model register dump |
| `make lockstep … DUMP_WAVES=1` | Same, plus VCD to `build/system_trace.vcd` |
| `make isa` | Full RISC-V ISA regression suite (84/84 expected) — progress per test |
| `make test` | ISA suite + quad-core vvadd pass/fail |
| `make ci-bench` | All 5 quad-core benchmark families must complete cleanly |
| `make smp-repro` | Multi-core repros: illegal-instruction trap + cross-hart `fence.i` |
| `make uartrx-test` | Console-input (uartlite RX) round trip through the RTL |
| `make profile BENCH=…` | Single-core cycle-accurate profile |
| `make profile-quad FAM=…` | Quad-core profile for one benchmark family |
| `make profile-all` | Quad-core profile for all benchmarks + chart |
| `make profile-all-sc` | Single-core profile for all benchmarks, all scales |
| `make fire [FIRE_FRAMES=N]` | Bare-metal Doom-fire demo |
| `make cube [FIRE_FRAMES=N]` | Rotating wireframe cube (fixed-point 3D, 4 harts) |
| `make solid [FIRE_FRAMES=N]` | Filled, shaded rotating cube (z-buffered, ~210 K cycles/frame) |
| `make linux-emu [LINUX_IMAGE=…]` | Interactive Linux shell on the golden model (fast) |
| `make linux-emu-check [LINUX_IMAGE=…]` | Scripted boot-to-login check (CI, non-interactive) |
| `make linux-sim [LINUX_IMAGE=…]` | Boot Linux on the Verilated RTL (live console, slow) |
| `make linux-sim DEBUG=1` | Same, plus a harness log and 20 M-cycle checkpoints |
| `make linux-sim RESUME=1` | Continue from the newest checkpoint |
| `make linux-ckpts` | List checkpoints available to resume from |
| `make linux-lockstep [LINUX_IMAGE=…]` | Bounded RTL-vs-emulator lock-step of the Linux boot (debug) |
| `make clean` | Remove generated artifacts (`build/`, `obj_dir`, logs) |
| `make distclean` | `clean` + drop sbt/Verilator build trees |
| `make help` | List all targets with descriptions |

`BENCH` = `<family>-s<scale>`. `FAM` = `<family>` alone (no scale).  
Families: `vvadd matmul filter csaxpy histo`. Scales: `s1`–`s5`. Default `BENCH`: `vvadd-s1`.

> The benchmark manifest (`mk/benchmarks.mk`) is the single source of truth for
> done-PCs and family names. Adding a workload is a one-line edit — no harness
> copy-paste required.

---

## Booting Linux (nommu, single-core & quad-core SMP)

chiron boots real Linux — a nommu, M-mode RISC-V kernel wrapped in bbl as a
flat binary loaded at `0x80000000`. Images are produced by the
[`mc-linux/`](mc-linux/) submodule (full pipeline and
every chiron-specific knob documented in its README):

```
cd mc-linux
./submodule_update                       # clone linux/ buildroot/ riscv-pk/
cd buildroot && make -j$(nproc) && cd .. # toolchain + rootfs (slow, once)
export RISCV=$PWD/buildroot/output/host
./apply_configs_and_patches              # stage chiron configs + patches
./build_image.sh s1                      # -> bins/linux-s1.bin (single-core)
./build_image.sh q4                      # -> bins/linux-q4.bin (quad-core SMP)
```

Then, from this repo root (RTL targets need `make sim` / `make sim-fast` first):

```
make linux-emu                             # quad-core shell on the golden model (default image)
make linux-emu  LINUX_IMAGE=bins/linux-s1.bin   # single-core variant
make linux-sim                             # boot the same image on the RTL (live console)
```

- **`linux-emu`** attaches the golden-model emulator to your terminal: it
  reaches `buildroot login:` in about a minute; log in as `root` (no password) —
  interactive input works (emulator UART RX + the kernel uartlite RX-poll patch).
  `linux-emu-check` is the non-interactive CI variant of the same boot.
- **`linux-sim`** boots the image on the Verilated RTL with a live uartlite
  console, and **stdin is wired through** — you can log in and run commands.
  Expect ~5–10K cycles/s: the kernel banner appears after ~20 minutes and the
  login prompt after ~3 billion cycles, so plan on leaving it overnight.
  **Its stdout is the guest console and nothing else**, so `make linux-sim |
  tee boot.log` produces a log of the boot rather than a log of the harness.
  `make uartrx-test` is the fast regression for the console-input path.

  A run that long should not have to be repeated from cycle zero, so the
  harness can snapshot itself:

  ```bash
  make linux-sim DEBUG=1            # + harness log (build/linux-sim.log) and a
                                    #   checkpoint every 20 M cycles
  make linux-ckpts                  # what is available to resume from
  make linux-sim DEBUG=1 RESUME=1   # continue from the newest checkpoint
  make linux-sim RESTORE=<file>     # or from a specific one
  ```

  `DEBUG=1` also turns on the progress heartbeat — cycles, steps/s, and all
  four harts' PCs every 100 K cycles — which is what distinguishes a slow boot
  from a wedge. It flags harts whose PC band is both tiny and unchanged
  between windows, the signature of a livelock. All of it goes to the log file;
  none of it touches the console.

  Checkpoints are ~256 MB each (the model contains all of DRAM), so `CKPT_KEEP`
  prunes to the newest 8 and none are written without `DEBUG=1`. Restoring is
  cycle-exact: a resumed run reproduces the original's step count and per-hart
  PCs beat for beat. A checkpoint is only valid for the netlist that wrote it —
  any RTL change invalidates every one, and the harness refuses to load a
  foreign or stale file rather than restoring garbage.
- `LINUX_IMAGE` defaults to `bins/linux-q4.bin` (see `mk/run.mk`).

**Status (2026-08-14): the quad-core image boots to an interactive shell on the
RTL.** All four harts come online, `/init` runs, and `nproc` answers `4` typed
live at the console — `docs/linux-quad-boot.log` is that boot.

Booting a real SMP kernel turned out to be the sharpest verification tool in the
project: it exercises coherence, speculation, and traps in combinations no
directed test reached, and every failure below was invisible to the ISA suite.
In the order they were found and fixed:

1. **CCU/L2 snoop starvation** — the D-cache clean-on-fence walker starved snoop
   requests behind its writebacks in the CCU's single serialized transaction
   FIFO. Fixed with a dedicated `readyCoherency` snoop path plus a flush-walker
   interlock.
2. **ROB livelock on coherent-load squash** — a snoop invalidating a load
   between D-cache response and commit triggered a whole-ROB rollback the FIFO's
   pointer arithmetic could not represent (`full` vs `empty` ambiguity), freezing
   the hart. Fixed with an explicit `flushAll` in `pipeline/Fifo.scala`.
3. **Two harts holding one line Unique** — the snoop path answered out of the
   writeback pipeline, which is correct for an eviction but not for a `fence.i`
   walker writeback, where the L1 still owns the line and may have stored into
   it since. Peers were handed pre-store data *with `PassDirty`*, silently losing
   a committed store. Fixed with the `retain` bit in `Dcache/traits.scala`.
4. **No illegal-instruction trap** — nothing drove `exceptionOccurred`, so a bad
   jump filled the ROB with zeros and stopped forever instead of faulting. The
   core now traps (`mcause=2`); `mt-illegal` is the regression, and note that
   pre-fix RTL *hangs* on it rather than failing.

One bug turned out not to be ours: on nommu RISC-V the kernel writes the
`rt_sigreturn` trampoline to the user stack with `copy_to_user` and jumps to it
without a `FENCE.I`. That is invisible on a machine whose I-fetch snoops the
D-caches; on chiron the fetch reads stale L2. The two-line kernel fix lives in
[`mc-linux/patches/`](mc-linux/patches/).

All of the above hold under the full suite: ISA 84/84, `ci-bench` 5/5,
`ci-check` 5/5, `smp-repro` 3/3.

---

## Known issues

- **`mask-sfilter` only has three distinct scales.** `s3.h`, `s4.h` and `s5.h`
  are byte-identical (`DATA_SIZE 156`), so `filter-s3`, `-s4` and `-s5` build the
  same image and unsurprisingly profile to the same number (1.472 aggregate IPC
  at all three). The flat tail of filter's scaling curve is a dataset artifact,
  not a saturation effect. Regenerating the larger datasets with
  `mask-sfilter_gendata.py` would fix it, at the cost of re-deriving that
  family's completion PCs and re-running the sweep.

---

## Roadmap

- Port the single-core IPC optimisations (radix-4 divider, store-data trim,
  load-queue flow-through) to the quad-core back-end. Aggregate quad-core IPC is
  currently **1.18–1.88** across the five families (see
  `docs/profile_report.png`), i.e. roughly 0.3–0.47 per core — the headroom is
  still large.
- Add an ownership check to the ROB execute write ports. Four separate bugs so
  far shared one shape: a completion landing on a slot that was rolled back and
  reallocated underneath it. A structural guard would close the class rather
  than the instances.
- Add **SPLASH-3** multi-threaded benchmarks for academically comparable results.
- Target IPC approaching 1.0 per core (4× aggregate) through microarchitectural
  tuning of the OoO window, commit width, and cache hierarchy.

---

## Credits
<!--
Built as a final-year project at the **University of Moratuwa**.

**Contributors** (in alphabetical order):
* Ajith Pasquel
* Hiruna Vishwamith
* Kavieesha Yalegama
* Leon Fernando
* Mewan Rathnayaka
* Yasiru Amarasinghe

!-->

Chisel/FIRRTL by the Chisel community; verification leans on **Verilator** and
the official **riscv-tests**. The Chiron artwork crowns a core meant, above all,
to teach.

<div align="center">
<sub>"The wisest of the Centaurs taught heroes. This core teaches how an out-of-order machine really works."</sub>
</div>
