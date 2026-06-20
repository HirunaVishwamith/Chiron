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
![ISA tests](https://img.shields.io/badge/riscv--tests-83%2F84-2ea44f)
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
- **Modern branch prediction** — bimodal + BTB (64 sets, 2-way LRU) + a
  **4-table TAGE** predictor.
- **Cycle-accurate, lock-step verified** against a C++ golden-model emulator,
  one committed instruction at a time — **84/84 official `riscv-tests` pass**.
- **One command per task.** No copying files around: every harness loads images
  by path, driven from a single benchmark manifest.
- **164 hardware performance counters** exposed from the RTL (41 per core ×4)
  — IPC, branch accuracy, cache miss rates, ROB-head stall decomposition,
  per-class latency attribution.

---

## See it run

```bash
make fire            # bare-metal Doom-fire demo, UART → terminal
```

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
| Commit width | 4-wide |
| Divider | Radix-4 (2 bits/cycle), clz-normalized |
| L1 I-Cache | 2-way · 64 sets · 16-instr lines |
| L1 D-Cache | 2-way · 64 sets · 8×8-byte lines |
| Branch predictor | Bimodal + BTB + 4-table TAGE |
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
├── src/main/scala/        # Chisel RTL (4-core system, frontend, decode, scheduler,
│                          #   rob, prf, caches, ACE interconnect, …)
├── emulator/              # C++ golden-model ISA simulator (4-hart, lock-step ref)
├── simulator/             # Verilator RTL wrapper
│   └── src/
│       ├── simulator.h    #   single-core signal accessors (core 0)
│       └── profiler_quad.h#   quad-core profiler (164 perf-counter signals)
├── harnesses/             # Test/run drivers
│   ├── lockstep.cpp       #   RTL-vs-emulator lock-step (core 0)
│   ├── lockstep_isa.cpp   #   ISA regression completion
│   ├── lockstep_linux.cpp #   Linux-boot variant
│   ├── profile.cpp        #   single-core cycle-accurate profiler
│   ├── profile_quad.cpp   #   quad-core profiler (all 4 cores + aggregate IPC)
│   └── fire.cpp           #   bare-metal UART → terminal streamer
├── workloads/
│   ├── benchmarks/        # benchmark sources (vvadd · matmul · filter · csaxpy · histo)
│   └── demos/             # bare-metal demos (fire 🔥)
├── bins/                  # staged .bin images
│   ├── mt-*-s1..s5.bin    #   single-core scaled variants
│   └── mt-*-q4.bin        #   quad-core (NUM_CORES=4) base variants
├── mk/                    # modular makefiles
│   ├── config.mk          #   paths, compiler flags
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

### 1 — Build the RTL

```bash
make sim        # Chisel → Verilog → Verilator library (~5 min first time)
```

> **Gotcha:** `make sim` always exits 0 even when `sbt` fails — it reuses a
> stale `system.v`. Always verify by spot-checking the generated file:
> ```bash
> grep "WLAST" system.v   # should have > 0 hits
> ```

### 2 — Full regression (what CI runs)

```bash
make test
```

Runs in two stages:

- **ISA suite** (`make isa`) — 84 official `riscv-tests` images, lock-step RTL
  vs golden model. Expected result: **83/84** (`rv64ui-p-fence_i` is a known
  I-cache coherence limitation).
- **Quad-core vvadd** (`make test-q4`) — profile-based pass/fail on
  `bins/mt-vvadd-q4.bin`.

### 3 — Profiling

#### Single quad-core benchmark

```bash
make profile-quad FAM=vvadd    # → build/profile_results/vvadd-q4.json
make profile-quad FAM=csaxpy
make profile-quad FAM=matmul
make profile-quad FAM=filter
make profile-quad FAM=histo
```

#### All quad-core benchmarks + chart

```bash
make profile-all
# → build/profile_results/<fam>-q4.json  (one per benchmark, 600 s timeout)
# → build/profile_results/profile_report.png
```

#### Single-core reference (all scales s1–s5)

```bash
make profile-all-sc
# → build/profile_results/<fam>-s<N>.json
```

#### Single benchmark, single-core, one scale

```bash
make profile BENCH=csaxpy-s2   # → build/profile_results/csaxpy-s2.json
```

### 4 — Debug a specific binary (lock-step with trace)

```bash
make lockstep BENCH=csaxpy-s2
# Writes run.log  states.log  regs.log  to build/
# Dumps VCD to   build/system_trace.vcd
```

Or directly with any image path:

```bash
./build/lockstep.out \
    --image bins/mt-csaxpy-s2.bin \
    --done-pc 0x800009a4 --done-pc 0x80000998 --done-a0 0 \
    --logdir build
```

### 5 — Golden emulator (no RTL, < 1 second)

```bash
make emu BENCH=csaxpy-s3
```

### 6 — Rebuild workload binaries

```bash
make bins        # single-core s1–s5 bins for all 5 families
make bins-q4     # quad-core -q4 bins (NUM_CORES=4) for all 5 families
make bins-all    # both
```

### Timing reference (Verilator, ~6 500 RTL cycles/sec)

| Benchmark | Approx wall time |
|---|---|
| vvadd-q4 | ~2 min |
| csaxpy-s2 | ~5 min |
| csaxpy-s5 / csaxpy-q4 | ~25 min |
| matmul / filter / histo (q4) | 5–15 min |

### Make target reference

| Target | What it does |
|---|---|
| `make sim` | Build the RTL (Chisel → Verilog → Verilator) |
| `make bins` | Build + stage single-core `.bin` images |
| `make bins-q4` | Build + stage quad-core `.bin` images (`-DNUM_CORES=4`) |
| `make bins-all` | Both of the above |
| `make emu BENCH=…` | Run benchmark on the golden emulator (fast, no RTL) |
| `make lockstep BENCH=…` | Lock-step RTL vs emulator; dumps logs + VCD |
| `make isa` | Full RISC-V ISA regression suite (83/84 expected) |
| `make test` | ISA suite + quad-core vvadd pass/fail |
| `make profile BENCH=…` | Single-core cycle-accurate profile |
| `make profile-quad FAM=…` | Quad-core profile for one benchmark family |
| `make profile-all` | Quad-core profile for all benchmarks + chart |
| `make profile-all-sc` | Single-core profile for all benchmarks, all scales |
| `make fire [FIRE_FRAMES=N]` | Bare-metal Doom-fire demo |
| `make linux BENCH=…` | Linux-boot lock-step harness |
| `make clean` | Remove generated artifacts (`build/`, `obj_dir`, logs) |
| `make distclean` | `clean` + drop sbt/Verilator build trees |
| `make help` | List all targets with descriptions |

`BENCH` = `<family>-s<scale>`. `FAM` = `<family>` alone (no scale).  
Families: `vvadd matmul filter csaxpy histo`. Scales: `s1`–`s5`. Default `BENCH`: `vvadd-s1`.

> The benchmark manifest (`mk/benchmarks.mk`) is the single source of truth for
> done-PCs and family names. Adding a workload is a one-line edit — no harness
> copy-paste required.

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

`make test` runs the full official `riscv-tests` suite (**84/84 pass**) plus
every benchmark. Divergences dump `run.log`, `states.log`, `regs.log` for
debugging.

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

## Roadmap

- Profile all 5 benchmarks at s1–s5 scales on the quad-core to establish a
  baseline IPC (currently ~0.125 — single-core IPC improvements not yet ported).
- Port the single-core IPC optimisations (radix-4 divider, store-data trim,
  load-queue flow-through) to the quad-core back-end.
- Add **SPLASH-3** multi-threaded benchmarks for academically comparable results.
- Target IPC approaching 1.0 per core (4× aggregate) through microarchitectural
  tuning of the OoO window, commit width, and cache hierarchy.

---

## Credits

Built as a final-year project at the **University of Moratuwa**.

**Contributors** (in alphabetical order):
* Ajith Pasquel
* Hiruna Vishwamith
* Kavieesha Yalegama
* Leon Fernando
* Mewan Rathnayaka
* Yasiru Amarasinghe

Chisel/FIRRTL by the Chisel community; verification leans on **Verilator** and
the official **riscv-tests**. The Chiron artwork crowns a core meant, above all,
to teach.

<div align="center">
<sub>"The wisest of the Centaurs taught heroes. This core teaches how an out-of-order machine really works."</sub>
</div>
