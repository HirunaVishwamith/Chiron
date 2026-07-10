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

# ── Fast no-trace model (the run-only path, e.g. linux-sim) ───────────────────
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
  CXX_FAST      := g++ -O3 -DCHIRON_NO_TRACE $(HARNESS_INCS_FAST) -DSTEP_TIMEOUT=500000
else
  VSYS_LIB_FAST := $(SIM_FAST)/Vsystem__ALL.a
  CXX_FAST      := g++ -O3 -DCHIRON_NO_TRACE $(HARNESS_INCS_FAST) -DSTEP_TIMEOUT=500000 $(VERILATED) $(VERILATED_VCD)
endif

# ── Linux boot harness flags ──────────────────────────────────────────────────
# linux-sim links the same walker-ON fast model as CI (obj_dir_fast). A larger
# STEP_TIMEOUT is used: legitimate boot phases (bbl kernel copy, rootfs) can
# idle-commit for a while without it being a wedge. The old walker-disabled
# obj_dir_linux path (sim-linux) is kept only for A/B debugging.
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

# Optional runtime diagnostic flags — passed to harness binaries at run time.
# Use: make lockstep SHOW_STATE=1   (print golden-model register state each step)
#      make lockstep DUMP_WAVES=1   (write VCD waveform to build/system_trace.vcd)
SHOW_STATE ?= 0
DUMP_WAVES ?= 0

$(BUILD):
	@mkdir -p $(BUILD)
