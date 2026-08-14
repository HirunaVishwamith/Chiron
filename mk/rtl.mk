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
	mv src/main/scala/common/configuration.scala configuration.txt && \
	sed 's/instructionBase/instructionBase = 0x0000000080000000L\/\//' configuration.txt \
	    > src/main/scala/common/configuration.scala && \
	(sbt "runMain system"; mv configuration.txt src/main/scala/common/configuration.scala)
	cp system.v $(SIM)/
	cd $(SIM)/; \
	cp ../../iCacheRegisters.v .; \
	for v in system.v iCacheRegisters.v; do \
	  for p in UNUSED DECLFILENAME VARHIDDEN WIDTH PINMISSING; do \
	    echo "/* verilator lint_off $$p */" | cat - $$v > tmp && mv tmp $$v; \
	  done; \
	done

# Trace model (obj_dir): Verilated with --trace for the lock-step debug harnesses
# (VCD via DUMP_WAVES=1). Generated C++ now compiled at -O3 (was -Os).
$(VSYS_LIB): $(SIM)/system.v
	cd $(SIM)/; \
	verilator -Wall --trace -cc system.v; \
	cd obj_dir/; make -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)"

# Fast model (obj_dir_fast): same Verilog, NO --trace, -O3 codegen — for the
# run-only harnesses (linux_sim). ~1.2x faster; cannot dump waveforms.
#
# --savable adds Verilator's model save/restore (VerilatedSave/VerilatedRestore).
# It only emits extra serialisation methods; it does not change simulation
# behaviour. It exists because the Linux SMP boot takes ~16 h to reach the
# post-/init hang, which made every hypothesis cost a full day. With
# checkpoints, a run snapshots itself periodically and a debug session restores
# just before the failure and iterates in minutes. It also makes cycle-bisection
# of a hang practical. See `make linux-ckpt` / CKPT_* in mk/run.mk.
#
# NOTE the checkpoint contains the whole model INCLUDING the 256 MB DRAM array
# (system.memory.memory), so each file is ~256 MB. CKPT_KEEP bounds how many are
# retained.
$(VSYS_LIB_FAST): $(SIM)/system.v
	cd $(SIM)/; \
	verilator -Wall -O3 --savable -cc system.v --Mdir obj_dir_fast; \
	cd obj_dir_fast/; make -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)"

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

$(VSYS_LIB_LINUX): $(SIM)/system_linux.v
	cd $(SIM)/; \
	verilator -Wall -O3 -cc system_linux.v --top-module system --Mdir obj_dir_linux; \
	cd obj_dir_linux/; make -j$(VJOBS) -f Vsystem.mk OPT_FAST="$(VOPT_FAST)"

.PHONY: sim sim-fast sim-linux
sim: $(VSYS_LIB)             ## Build the RTL: Chisel → Verilog → Verilator library
sim-fast: $(VSYS_LIB_FAST)   ## Build the fast no-trace RTL model (used by benchmarks + linux-sim)
sim-linux: $(VSYS_LIB_LINUX) ## Optional: walker-disabled model (A/B debug only)

# ── Kintex-7 FPGA flavour (quad-core, PCIe/XDMA — see fpga/) ──────────────────
# Same instructionBase as sim (0x8000_0000): fpgaTop reuses chironCore
# unchanged (see testbench/fpgaTop.scala), just swaps mainMemory/MultiUart's
# tie-offs for real DDR3 (MIG)/host-bridge wiring. Output copied into
# fpga/hdl/ where fpga/build_kintex7.tcl's source-file globbing picks it up.
.PHONY: fpga-verilog
fpga-verilog:   ## Generate fpgaTop.v for the Kintex-7 build (fpga/hdl/fpgaTop.v)
	mv src/main/scala/common/configuration.scala configuration.txt && \
	sed 's/instructionBase/instructionBase = 0x0000000080000000L\/\//' configuration.txt \
	    > src/main/scala/common/configuration.scala && \
	(sbt "runMain fpgaTop"; mv configuration.txt src/main/scala/common/configuration.scala)
	@mkdir -p fpga/hdl
	cp fpgaTop.v fpga/hdl/

# ── Zynq FPGA flavour (instructionBase = 0x4000_0000 + boot ROM + PS CLINT) ───
.PHONY: zynq
zynq:                   ## Generate FPGA Verilog (Zynq base) + boot ROM + vivado.tcl
	mv src/main/scala/common/configuration.scala configuration.txt && \
	sed 's/instructionBase/instructionBase = 0x0000000040000000L\/\//' configuration.txt \
	    > src/main/scala/common/configuration.scala && \
	(sbt "runMain core"; mv configuration.txt src/main/scala/common/configuration.scala)
	sbt "runMain bootROM"
	sbt "runMain testbench.psClint"
	cp src/main/resources/zynq/vivado.tcl .
