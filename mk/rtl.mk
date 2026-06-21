# ── RTL build: Chisel → Verilog → Verilator library ──────────────────────────
SCALA_SRCS := $(shell find src/main/scala/ -type f -name '*.scala')

# The Verilator library is the real build artifact; it rebuilds whenever any
# Scala source changes. Recipe = the proven flow: patch instructionBase to the
# sim RAM base (0x8000_0000), run sbt, lint-suppress, verilate, compile. The
# configuration.scala patch is always reverted, even if sbt fails.
$(VSYS_LIB): $(SCALA_SRCS)
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
	done; \
	verilator -Wall --trace -cc system.v; \
	cd obj_dir/; make -f Vsystem.mk

.PHONY: sim
sim: $(VSYS_LIB)        ## Build the RTL: Chisel → Verilog → Verilator library

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
