# ╔══════════════════════════════════════════════════════════════════════════╗
# ║  Chiron — RV64IMA out-of-order superscalar core                            ║
# ║  Thin orchestrator. Real logic lives in mk/*.mk. One entry point per task; ║
# ║  nothing here copies files around — every harness loads images by path.    ║
# ╚══════════════════════════════════════════════════════════════════════════╝
#
#   make sim                       Build the RTL (Chisel → Verilog → Verilator)
#   make bins                      Build + stage every workload .bin into bins/
#   make emu       BENCH=vvadd-s1  Run a benchmark on the golden emulator (fast)
#   make lockstep  BENCH=matmul-s2 Lock-step the RTL against the emulator
#   make profile   BENCH=vvadd-s1  Cycle-accurate IPC / stall profile
#   make isa                       Full RISC-V ISA regression suite
#   make test                      ISA suite + every benchmark (lock-step)
#   make fire     [FIRE_FRAMES=N]  Render the bare-metal Doom-fire demo
#
# BENCH defaults to vvadd-s1. Families: vvadd matmul filter csaxpy histo (×s1..s5).

include mk/config.mk
include mk/benchmarks.mk
include mk/rtl.mk
include mk/bins.mk
include mk/bins_quad.mk
include mk/run.mk

.DEFAULT_GOAL := help

.PHONY: help
help:   ## Show this help
	@echo "Chiron — make targets:"
	@grep -hE '^[a-zA-Z0-9_-]+:.*?##' $(MAKEFILE_LIST) \
	  | sort | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'
	@echo
	@echo "  BENCH=<family>-s<scale>  (default vvadd-s1; families: $(BENCHES))"

.PHONY: clean distclean
clean:   ## Remove generated artifacts (build/, obj_dir, logs)
	rm -rf $(BUILD)
	rm -rf $(SIM)/obj_dir
	rm -f  $(SIM)/system.v $(SIM)/iCacheRegisters.v
	rm -f  run.log states.log regs.log test_results.txt system_trace.vcd
	rm -f  system.v system.fir system.anno.json .stamp.*

distclean: clean   ## clean + drop the sbt/Verilator build trees
	rm -rf target project/target project/project
	$(TOOLPATH) $(MAKE) -C $(DEMO_SRC) clean  2>/dev/null || true
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) clean 2>/dev/null || true

.PHONY: fix-inotify
fix-inotify:   ## Raise inotify limit if sbt hangs on file watches
