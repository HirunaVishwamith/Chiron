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
	cd obj_dir/; make -f Vsystem.mk OPT_FAST="$(VOPT_FAST)"

# Fast model (obj_dir_fast): same Verilog, NO --trace, -O3 codegen — for the
# run-only harnesses (linux_sim). ~1.2x faster; cannot dump waveforms.
$(VSYS_LIB_FAST): $(SIM)/system.v
	cd $(SIM)/; \
	verilator -Wall -O3 -cc system.v --Mdir obj_dir_fast; \
	cd obj_dir_fast/; make -f Vsystem.mk OPT_FAST="$(VOPT_FAST)"

# ── Linux model (fence.i walker disabled) ─────────────────────────────────────
# Same Chisel → Verilog flow as system.v, but also patches
# configuration.disableFenceIWalker to true so the fence.i clean-on-fence walker
# is compiled out. That walker is required for the rv64ui-p-fence_i ISA test but
# deadlocks bbl's large fence.i during Linux SMP boot; the Linux boot model omits
# it. sbt always emits root system.v, so we copy it aside to system_linux.v. NOTE:
# this shares configuration.scala + root system.v with the `sim` rule, so do not
# build `sim` and `sim-linux` concurrently.
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

# Verilate the walker-disabled Verilog into its own obj_dir_linux (top module is
# still `system`, so the class is Vsystem, same as the other models).
$(VSYS_LIB_LINUX): $(SIM)/system_linux.v
	cd $(SIM)/; \
	verilator -Wall -O3 -cc system_linux.v --top-module system --Mdir obj_dir_linux; \
	cd obj_dir_linux/; make -f Vsystem.mk OPT_FAST="$(VOPT_FAST)"

.PHONY: sim sim-fast sim-linux
sim: $(VSYS_LIB)             ## Build the RTL: Chisel → Verilog → Verilator library
sim-fast: $(VSYS_LIB_FAST)   ## Build the fast no-trace RTL model (used by benchmarks)
sim-linux: $(VSYS_LIB_LINUX) ## Build the walker-disabled RTL model (for linux-sim boot)

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
