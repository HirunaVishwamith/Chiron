# ── Chiron build configuration ───────────────────────────────────────────────
# Directories, toolchains and the host-compiler invocations shared by every
# other fragment. Tune paths here; nothing else hard-codes them.

SHELL := /bin/bash

# Project directory layout (no inline comments — trailing space corrupts the value)
# All host-side C++ lives under sim/: emulator (golden model), rtl (Verilator
# wrapper), harness (drivers), tests (ISA images), data (runtime inputs).
BUILD     := build                 # all generated artifacts (gitignored)
BINS      := bins                  # runnable .bin images (built + staged)
HARNESS   := sim/harness           # C++ test/run drivers
EMU       := sim/emulator          # golden-model ISA emulator
SIM       := sim/rtl               # Verilator RTL wrapper
ISA_DIR   := sim/tests/riscv-isa   # RISC-V ISA regression images
DATA      := sim/data              # runtime inputs (Image, qemu.dtb, boot.bin)
BENCH_SRC := workloads/benchmarks  # benchmark sources (was Mt-Benchmark)
DEMO_SRC  := workloads/demos       # bare-metal demos (was Mt-Tinyprograms)
# Strip any trailing whitespace the aligned comments above introduced.
BUILD     := $(strip $(BUILD))
BINS      := $(strip $(BINS))
HARNESS   := $(strip $(HARNESS))
EMU       := $(strip $(EMU))
SIM       := $(strip $(SIM))
ISA_DIR   := $(strip $(ISA_DIR))
DATA      := $(strip $(DATA))
BENCH_SRC := $(strip $(BENCH_SRC))
DEMO_SRC  := $(strip $(DEMO_SRC))

# Verilator
VINC     := /usr/share/verilator/include
VSYS_LIB := $(SIM)/obj_dir/Vsystem__ALL.a

# RISC-V bare-metal cross toolchain (prepended to PATH for the .bin builds only)
RISCV_BIN := /media/hv/D1/OOO_Processor/riscv/bin
TOOLPATH  := PATH=$(RISCV_BIN):$$PATH

# Default runtime image (harnesses fall back to this when --image is omitted)
EMU_IMAGE := $(DATA)/Image

# Host-compiler invocations for the Verilator harnesses. These run from the repo
# root, so "-I ." lets the harness quote-includes resolve sim/emulator/ and
# sim/rtl/ headers. Lock-step harnesses dump a VCD (need verilated_vcd_c);
# the profiler and the fire viz do not.
HARNESS_INCS  := -I . -I $(VINC) -I $(SIM)/obj_dir
VERILATED     := $(VINC)/verilated.cpp
VERILATED_VCD := $(VINC)/verilated_vcd_c.cpp
# The checkpoint model (obj_dir_save, --savable) is used by linux-sim. Verilator
# 4.x emits calls into VerilatedSerialize/VerilatedDeserialize but does NOT
# bundle their implementation into Vsystem__ALL.a, so this must be compiled
# alongside or the link fails with `undefined reference to
# VerilatedDeserialize::readAssert`.
VERILATED_SAVE := $(VINC)/verilated_save.cpp

# Verilator 5.x splits the runtime into a separate libverilated.a (containing
# verilated.o + verilated_vcd_c.o + verilated_threads.o). Verilator 4.x bundles
# everything into Vsystem__ALL.a and has no libverilated.a.
# Detect which world we're in: if libverilated.a is present (built by make sim),
# link against it; otherwise compile the verilated sources directly.
_VLIB := $(wildcard $(SIM)/obj_dir/libverilated.a)
ifneq ($(_VLIB),)
  # 5.x: link against pre-built runtime (includes VlThreadPool etc.)
  VSYS_LIB  := $(_VLIB) $(SIM)/obj_dir/Vsystem__ALL.a
  CXX_TRACE   := g++ -O3 $(HARNESS_INCS) -DSTEP_TIMEOUT=500000
  CXX_NOTRACE := g++ -O3 $(HARNESS_INCS) -I $(SIM)
else
  # 4.x: runtime is bundled in Vsystem__ALL.a; compile verilated sources ourselves
  VSYS_LIB  := $(SIM)/obj_dir/Vsystem__ALL.a
  CXX_TRACE   := g++ -O3 $(HARNESS_INCS) -DSTEP_TIMEOUT=500000 $(VERILATED) $(VERILATED_VCD)
  CXX_NOTRACE := g++ -O3 $(HARNESS_INCS) -I $(SIM) $(VERILATED)
endif

# ── Fast no-trace model (benches, ci-check, linux-sim-fast) ───────────────────
# Same RTL as obj_dir, but Verilated WITHOUT --trace and with -O3 codegen — for
# long runs (a full Linux boot) where VCD is never dumped. Harnesses link with
# -DCHIRON_NO_TRACE so rtl_model.h's single tb->trace() call compiles out. The
# behaviour-changing --x-assign/--x-initial fast flags are deliberately NOT used
# (they wedge the uartlite MMIO read path); only behaviour-neutral flags here.
SIM_FAST          := $(SIM)/obj_dir_fast
HARNESS_INCS_FAST := -I . -I $(VINC) -I $(SIM_FAST)
_VLIB_FAST := $(wildcard $(SIM_FAST)/libverilated.a)
ifneq ($(_VLIB_FAST),)
  VSYS_LIB_FAST := $(_VLIB_FAST) $(SIM_FAST)/Vsystem__ALL.a
  CXX_FAST      := g++ -O3 -DCHIRON_NO_TRACE $(HARNESS_INCS_FAST) -DSTEP_TIMEOUT=500000 $(VERILATED_SAVE)
else
  VSYS_LIB_FAST := $(SIM_FAST)/Vsystem__ALL.a
  CXX_FAST      := g++ -O3 -DCHIRON_NO_TRACE $(HARNESS_INCS_FAST) -DSTEP_TIMEOUT=500000 $(VERILATED) $(VERILATED_VCD) $(VERILATED_SAVE)
endif

# ── Linux boot harness flags ──────────────────────────────────────────────────
# linux-sim-fast links the same walker-ON fast model as CI (obj_dir_fast).
# linux-sim itself links obj_dir_save (--savable). A larger STEP_TIMEOUT is
# used: legitimate boot phases (bbl kernel copy, rootfs) can idle-commit for
# a while without it being a wedge. The old walker-disabled obj_dir_linux
# path (sim-linux) is kept only for A/B debugging.
SIM_LINUX          := $(SIM)/obj_dir_linux
HARNESS_INCS_LINUX := -I . -I $(VINC) -I $(SIM_LINUX)
_VLIB_LINUX := $(wildcard $(SIM_LINUX)/libverilated.a)
ifneq ($(_VLIB_LINUX),)
  VSYS_LIB_LINUX := $(_VLIB_LINUX) $(SIM_LINUX)/Vsystem__ALL.a
else
  VSYS_LIB_LINUX := $(SIM_LINUX)/Vsystem__ALL.a
endif
# Prefer the fast-model include path (single-build fix); fall back to compiling
# verilated sources ourselves on Verilator 4.x.
ifneq ($(_VLIB_FAST),)
  CXX_LINUX := g++ -O3 -DCHIRON_NO_TRACE $(HARNESS_INCS_FAST) -DSTEP_TIMEOUT=5000000
else
  CXX_LINUX := g++ -O3 -DCHIRON_NO_TRACE $(HARNESS_INCS_FAST) -DSTEP_TIMEOUT=5000000 $(VERILATED) $(VERILATED_VCD)
endif

# Optimization for the Verilator-generated C++ (verilated.mk's OPT_FAST default
# is -Os = size; -O3 is markedly faster for long simulations). Behaviour-neutral.
VOPT_FAST ?= -O3 -march=native -fno-math-errno

# Host cores. Used only for the Verilator C++ compile (VJOBS). Do not feed
# this into ci-bench / ci-check: running several RTL sims at once OOMs or
# flakes GitHub-hosted runners. Default CI_JOBS is 1; override locally if
# you have the RAM. --savable is already off on the fast model, so a
# sequential 5-bench gate is the old 12–14 min path, not the 27 min one.
NPROC := $(shell nproc 2>/dev/null || echo 1)

# Parallelism for compiling the Verilated C++. Verilator splits system.v into
# many translation units and each takes tens of seconds at -O3, so building
# them one at a time costs 15-20 min. Local `make sim` still uses nproc;
# CI does not pass VJOBS (the workflow must not enable nproc).
VJOBS   ?= $(NPROC)
# How many of the five ci-bench / ci-check simulations to run at once.
# Default 1: one Verilated DRAM (~256 MB) at a time. Do not default this
# to nproc — that is what fails CI.
CI_JOBS ?= 1

# Verilator CLI flags shared by every model. --output-split is what sets
# VM_PARALLEL_BUILDS=1 in Vsystem.mk; without it some versions concatenate
# every .cpp into one translation unit and `make -j` compiles a single file.
# --no-timing is 5.x-only: 5.x default timing-eval is much slower and we
# do not use SV timing. 4.038 rejects the flag, so it is added only on 5.x.
VERILATOR_MAJ := $(shell verilator --version 2>/dev/null | awk '{print int($$2)}')
VFLAGS_COMMON := -Wall --output-split 20000
ifeq ($(VERILATOR_MAJ),5)
VFLAGS_COMMON += --no-timing
endif

# Optional runtime diagnostic flags — passed to harness binaries at run time.
# Use: make lockstep SHOW_STATE=1   (print golden-model register state each step)
#      make lockstep DUMP_WAVES=1   (write VCD waveform to build/system_trace.vcd)
#      make lockstep-q4 …           (same flags; 4-hart compare)
SHOW_STATE ?= 0
DUMP_WAVES ?= 0

$(BUILD):
	@mkdir -p $(BUILD)
