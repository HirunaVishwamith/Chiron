# ── Chiron build configuration ───────────────────────────────────────────────
# Directories, toolchains and the host-compiler invocations shared by every
# other fragment. Tune paths here; nothing else hard-codes them.

SHELL := /bin/bash

# Project directory layout (no inline comments — trailing space corrupts the value)
BUILD     := build                 # all generated artifacts (gitignored)
BINS      := bins                  # runnable .bin images (built + staged)
HARNESS   := harnesses             # C++ test/run drivers
EMU       := emulator              # golden-model ISA emulator (was fyp18-riscv-emulator)
SIM       := simulator/src         # Verilator RTL wrapper
BENCH_SRC := workloads/benchmarks  # benchmark sources (was Mt-Benchmark)
DEMO_SRC  := workloads/demos       # bare-metal demos (was Mt-Tinyprograms)
# Strip any trailing whitespace the aligned comments above introduced.
BUILD     := $(strip $(BUILD))
BINS      := $(strip $(BINS))
HARNESS   := $(strip $(HARNESS))
EMU       := $(strip $(EMU))
SIM       := $(strip $(SIM))
BENCH_SRC := $(strip $(BENCH_SRC))
DEMO_SRC  := $(strip $(DEMO_SRC))

# Verilator
VINC     := /usr/share/verilator/include
VSYS_LIB := $(SIM)/obj_dir/Vsystem__ALL.a

# RISC-V bare-metal cross toolchain (prepended to PATH for the .bin builds only)
RISCV_BIN := /media/hv/D1/OOO_Processor/riscv/bin
TOOLPATH  := PATH=$(RISCV_BIN):$$PATH

# Legacy fixed image path (kept so the emulator's default still resolves)
EMU_IMAGE := $(EMU)/src/Image

# Host-compiler invocations for the Verilator harnesses. These run from the repo
# root, so "-I ." lets the harness quote-includes resolve emulator/ and
# simulator/ headers. Lock-step harnesses dump a VCD (need verilated_vcd_c);
# the profiler and the fire viz do not.
HARNESS_INCS  := -I . -I $(VINC) -I $(SIM)/obj_dir
VERILATED     := $(VINC)/verilated.cpp
VERILATED_VCD := $(VINC)/verilated_vcd_c.cpp
CXX_TRACE     := g++ -O3 $(HARNESS_INCS) -DSTEP_TIMEOUT=500000 $(VERILATED) $(VERILATED_VCD)
CXX_NOTRACE   := g++ -O3 $(HARNESS_INCS) -I $(SIM) $(VERILATED)

# Optional runtime diagnostic flags — passed to harness binaries at run time.
# Use: make lockstep SHOW_STATE=1   (print golden-model register state each step)
#      make lockstep DUMP_WAVES=1   (write VCD waveform to build/system_trace.vcd)
SHOW_STATE ?= 0
DUMP_WAVES ?= 0

$(BUILD):
	@mkdir -p $(BUILD)
