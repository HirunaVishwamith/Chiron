# ── RTL build: Chisel → Verilog → Verilator library ──────────────────────────
SCALA_SRCS := $(shell find src/main/scala/ -type f -name '*.scala')

# The Verilator library is the real build artifact; it rebuilds whenever any
# Scala source changes. Recipe = the proven flow: patch instructionBase to the
# sim RAM base (0x8000_0000), run sbt, lint-suppress, verilate, compile. The
# configuration.scala patch is always reverted, even if sbt fails.
# Chisel → (patched, lint-suppressed) Verilog. Shared by both Verilator builds
# below so the sbt run happens once. Patches instructionBase to the sim RAM base
# (0x8000_0000); the configuration.scala patch is always reverted, even if sbt
# fails.
$(SIM)/system.v: $(SCALA_SRCS)
	@set -e; \
	cp src/main/scala/common/configuration.scala configuration.txt; \
	sed 's/instructionBase/instructionBase = 0x0000000080000000L\/\//' configuration.txt \
	    > src/main/scala/common/configuration.scala; \
	if ! sbt "runMain system"; then \
	  mv configuration.txt src/main/scala/common/configuration.scala; \
	  echo "[rtl] sbt FAILED — refusing to reuse a stale system.v"; \
	  exit 1; \
	fi; \
	mv configuration.txt src/main/scala/common/configuration.scala; \
	test -s system.v
	cp system.v $(SIM)/
	cd $(SIM)/; \
	cp ../../iCacheRegisters.v .; \
	for v in system.v iCacheRegisters.v; do \
	  for p in UNUSED DECLFILENAME VARHIDDEN WIDTH PINMISSING; do \
	    echo "/* verilator lint_off $$p */" | cat - $$v > tmp && mv tmp $$v; \
	  done; \
	done

# Every model directory is WIPED before re-verilating. --output-split names the
# generated translation units by ordinal (Vsystem__1.cpp, Vsystem_fetch__13.cpp,
# ...), so when the netlist changes size Verilator emits a DIFFERENT NUMBER of
# them. `ar -cr` then merges the new objects into the archive that still holds
# the old ones, and the link either fails with "multiple definition of
# Vsystem_fetch::_sequent__TOP__...fetch__10" or -- worse -- silently resolves
# some symbols out of stale objects, giving a model that is part old netlist.
# Nothing is lost by wiping: verilator rewrites every .cpp anyway, so the whole
# directory recompiles on any system.v change regardless.
VERILATE = rm -rf $(1); verilator

# Trace model (obj_dir): Verilated with --trace for the lock-step debug harnesses
# (VCD via DUMP_WAVES=1). Generated C++ now compiled at -O3 (was -Os).
$(VSYS_LIB): $(SIM)/system.v mk/rtl.mk mk/config.mk
	cd $(SIM)/; \
	$(call VERILATE,obj_dir) $(VFLAGS_COMMON) --trace -cc system.v; \
	$(MAKE) -C obj_dir -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)" VM_PARALLEL_BUILDS=1

# Fast model (obj_dir_fast): same Verilog, NO --trace, NO --savable, -O3
# codegen, --threads. Used by ci-bench / ci-check / profile / linux-sim-fast.
# Checkpoints live on sim-ckpt (obj_dir_save) so this path does not pay
# --savable's extra generated C++ (~150 TUs).
#
# Threading is a property of the MODEL, not of a separate target: every boot and
# benchmark path gets it. Verilator statically partitions the eval graph across
# VTHREADS workers, which is a pure host-side transformation -- execution stays
# bit-identical (verified: retired-instruction counts match cycle-for-cycle
# against the single-threaded model). Set VTHREADS=1 to opt out.
$(VSYS_LIB_FAST): $(SIM)/system.v mk/rtl.mk mk/config.mk
	cd $(SIM)/; \
	$(call VERILATE,obj_dir_fast) $(VFLAGS_COMMON) -O3 --threads $(VTHREADS) -cc system.v --Mdir obj_dir_fast; \
	$(MAKE) -C obj_dir_fast -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)" VM_PARALLEL_BUILDS=1

# ── Optional walker-disabled model (A/B debug only) ───────────────────────────
# Same Chisel → Verilog flow as system.v, but patches
# configuration.disableFenceIWalker to true. NOT required for linux-sim anymore
# (walkerWriteBackBuffer fixes the SMP fence.i circular wait in the default
# model). Kept so `make sim-linux` can still build a walker-off binary for
# bisect/debug. Do not build `sim` and `sim-linux` concurrently (shared sbt).
$(SIM)/system_linux.v: $(SCALA_SRCS)
	mv src/main/scala/common/configuration.scala configuration.txt && \
	sed -e 's/instructionBase/instructionBase = 0x0000000080000000L\/\//' \
	    -e 's/val disableFenceIWalker = false/val disableFenceIWalker = true/' \
	    configuration.txt > src/main/scala/common/configuration.scala && \
	(sbt "runMain system"; mv configuration.txt src/main/scala/common/configuration.scala)
	cp system.v $(SIM)/system_linux.v
	cd $(SIM)/; \
	cp ../../iCacheRegisters.v .; \
	for v in system_linux.v iCacheRegisters.v; do \
	  for p in UNUSED DECLFILENAME VARHIDDEN WIDTH PINMISSING; do \
	    echo "/* verilator lint_off $$p */" | cat - $$v > tmp && mv tmp $$v; \
	  done; \
	done

$(VSYS_LIB_LINUX): $(SIM)/system_linux.v mk/rtl.mk mk/config.mk
	cd $(SIM)/; \
	$(call VERILATE,obj_dir_linux) $(VFLAGS_COMMON) -O3 -cc system_linux.v --top-module system --Mdir obj_dir_linux; \
	$(MAKE) -C obj_dir_linux -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)" VM_PARALLEL_BUILDS=1

# Checkpointable copy of the fast model (--savable). Only linux-sim and the
# Linux debug probes need this; CI never builds it.
SIM_CKPT          := $(SIM)/obj_dir_save
VSYS_LIB_CKPT     := $(SIM_CKPT)/Vsystem__ALL.a
HARNESS_INCS_CKPT := -I . -I $(VINC) -I $(SIM_CKPT)
# Threaded as well: --threads and --savable coexist (the combined model emits
# both VlThreadPool and VerilatedSerialize/Deserialize), so the checkpointing
# boot -- `make linux-sim`, DEBUG=1 -- runs at full speed AND stays resumable.
CXX_CKPT          := g++ -O3 -DVL_THREADED=1 -pthread -DCHIRON_NO_TRACE \
                     $(HARNESS_INCS_CKPT) -DSTEP_TIMEOUT=5000000 \
                     $(VERILATED) $(VERILATED_VCD) $(VERILATED_SAVE) \
                     $(VINC)/verilated_threads.cpp

$(VSYS_LIB_CKPT): $(SIM)/system.v mk/rtl.mk mk/config.mk
	cd $(SIM)/; \
	$(call VERILATE,obj_dir_save) $(VFLAGS_COMMON) -O3 --threads $(VTHREADS) --savable -cc system.v --Mdir obj_dir_save; \
	$(MAKE) -C obj_dir_save -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)" VM_PARALLEL_BUILDS=1

.PHONY: sim sim-fast sim-ckpt sim-linux
sim: $(VSYS_LIB)             ## Build the RTL: Chisel → Verilog → Verilator library
sim-fast: $(VSYS_LIB_FAST)   ## Threaded no-trace model: benches, ci-check, linux-sim-fast
sim-ckpt: $(VSYS_LIB_CKPT)   ## Threaded model + --savable (linux-sim checkpoints)
sim-linux: $(VSYS_LIB_LINUX) ## Optional: walker-disabled model (A/B debug only)

# ── Kintex-7 FPGA flavour (quad-core, PCIe/XDMA — see fpga/) ──────────────────
# Same instructionBase as sim (0x8000_0000): fpgaTop reuses chironCore
# unchanged (see testbench/fpgaTop.scala), just swaps mainMemory/MultiUart's
# tie-offs for real DDR3 (MIG)/host-bridge wiring. Output copied into
# fpga/hdl/ where fpga/build_kintex7.tcl's source-file globbing picks it up.
.PHONY: fpga-verilog
fpga-verilog:   ## Generate fpgaTop.v for the Kintex-7 build (fpga/hdl/fpgaTop.v)
	@set -e; \
	cp src/main/scala/common/configuration.scala configuration.txt; \
	sed 's/instructionBase/instructionBase = 0x0000000080000000L\/\//' configuration.txt \
	    > src/main/scala/common/configuration.scala; \
	if ! sbt "runMain fpgaTop"; then \
	  mv configuration.txt src/main/scala/common/configuration.scala; \
	  echo "[rtl] sbt FAILED — not copying a stale fpgaTop.v"; \
	  exit 1; \
	fi; \
	mv configuration.txt src/main/scala/common/configuration.scala; \
	test -s fpgaTop.v
	@mkdir -p fpga/hdl
	cp fpgaTop.v fpga/hdl/

# ── Zynq FPGA flavour (instructionBase = 0x4000_0000 + boot ROM + PS CLINT) ───
.PHONY: zynq
zynq:                   ## Generate FPGA Verilog (Zynq base) + boot ROM + vivado.tcl
	@set -e; \
	cp src/main/scala/common/configuration.scala configuration.txt; \
	sed 's/instructionBase/instructionBase = 0x0000000040000000L\/\//' configuration.txt \
	    > src/main/scala/common/configuration.scala; \
	if ! sbt "runMain core"; then \
	  mv configuration.txt src/main/scala/common/configuration.scala; \
	  echo "[rtl] sbt FAILED — not emitting stale Zynq Verilog"; \
	  exit 1; \
	fi; \
	mv configuration.txt src/main/scala/common/configuration.scala
	sbt "runMain bootROM"
	sbt "runMain testbench.psClint"
	cp src/main/resources/zynq/vivado.tcl .
