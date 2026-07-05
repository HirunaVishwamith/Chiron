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

.PHONY: sim sim-fast
sim: $(VSYS_LIB)             ## Build the RTL: Chisel → Verilog → Verilator library
sim-fast: $(VSYS_LIB_FAST)   ## Build the fast no-trace RTL model (used by linux-sim)

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
