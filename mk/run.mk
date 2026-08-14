# ── Harness binaries (compiled into $(BUILD)) ────────────────────────────────
# All emulator headers/fragments emulator.h pulls in, so any change to the
# golden model (hart.h, the hart_*.inc fragments, terminal.h, …) forces a
# rebuild of the harnesses that embed it.
EMU_HDRS := $(EMU)/emulator.h $(EMU)/constants.h $(EMU)/hart.h \
            $(wildcard $(EMU)/hart_*.inc) $(EMU)/terminal.h $(EMU)/clint.h
SIM_HDR  := $(SIM)/rtl_model.h

$(BUILD)/lockstep.out: $(HARNESS)/lockstep.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/lockstep.cpp $(VSYS_LIB) -o $@

$(BUILD)/lockstep_isa.out: $(HARNESS)/lockstep_isa.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/lockstep_isa.cpp $(VSYS_LIB) -o $@

$(BUILD)/lockstep_linux.out: $(HARNESS)/lockstep_linux.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/lockstep_linux.cpp $(VSYS_LIB) -o $@

# Same lock-step compare, linked against the fast no-trace model: ~4-5x faster,
# no VCD on mismatch (rtl_model.h prints a notice and skips tracing). Use this
# to *reach* a deep-boot mismatch quickly; switch to lockstep_linux.out when a
# waveform of the failure is needed.
$(BUILD)/lockstep_linux_fast.out: $(HARNESS)/lockstep_linux.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/lockstep_linux.cpp $(VSYS_LIB_FAST) -o $@

# Debug probes (sim/harness/probes/): RTL-only Linux boot with targeted
# internal-signal logging. ccu_line_probe watches CCU transactions/beats on
# hardwired victim/source lines (edit the watched() ranges in the source);
# div_park_probe dumps M-unit + scheduler queue state when a hart's ROB head
# parks. Both link the fast no-trace model.
$(BUILD)/ccu_line_probe.out: $(HARNESS)/probes/ccu_line_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/ccu_line_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/div_park_probe.out: $(HARNESS)/probes/div_park_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/div_park_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/wedge_dump_probe.out: $(HARNESS)/probes/wedge_dump_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/wedge_dump_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/tag_dump_probe.out: $(HARNESS)/probes/tag_dump_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/tag_dump_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/ipi_hang_probe.out: $(HARNESS)/probes/ipi_hang_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/ipi_hang_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/tty_scan_probe.out: $(HARNESS)/probes/tty_scan_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/tty_scan_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/ipitmr_wedge_probe.out: $(HARNESS)/probes/ipitmr_wedge_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/ipitmr_wedge_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/msip_arb_probe.out: $(HARNESS)/probes/msip_arb_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/msip_arb_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/ipitmr_loop_probe.out: $(HARNESS)/probes/ipitmr_loop_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/ipitmr_loop_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/ipitmr_ctx_probe.out: $(HARNESS)/probes/ipitmr_ctx_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/ipitmr_ctx_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/inject_fsm_probe.out: $(HARNESS)/probes/inject_fsm_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/inject_fsm_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/rob_integrity_probe.out: $(HARNESS)/probes/rob_integrity_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/rob_integrity_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/arbiter_race_probe.out: $(HARNESS)/probes/arbiter_race_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/arbiter_race_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/periph_squash_probe.out: $(HARNESS)/probes/periph_squash_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/periph_squash_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/arbiter_stall_probe.out: $(HARNESS)/probes/arbiter_stall_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/arbiter_stall_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/injwedge_probe.out: $(HARNESS)/probes/injwedge_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/injwedge_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/brswallow_probe.out: $(HARNESS)/probes/brswallow_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/brswallow_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/brleak_probe.out: $(HARNESS)/probes/brleak_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/brleak_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/robmodify_probe.out: $(HARNESS)/probes/robmodify_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/robmodify_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/robhead_probe.out: $(HARNESS)/probes/robhead_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/robhead_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/brvanish_probe.out: $(HARNESS)/probes/brvanish_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/brvanish_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/robready_probe.out: $(HARNESS)/probes/robready_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/robready_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/mextpath_probe.out: $(HARNESS)/probes/mextpath_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/mextpath_probe.cpp $(VSYS_LIB_FAST) -o $@

# Boots the full Linux image while counting CLINT msip writes per target hart.
# Uses CXX_LINUX (raised STEP_TIMEOUT) like linux_sim.out, since legitimate boot
# phases stall far longer than a benchmark ever does.
$(BUILD)/linux_ipi_probe.out: $(HARNESS)/probes/linux_ipi_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_ipi_probe.cpp $(VSYS_LIB_FAST) -o $@

# Restores a linux_ipi_probe checkpoint and dissects the csd_lock_wait hang:
# reads hart1's a4 (the csd address), what its reload actually returns, and the
# same word straight out of DRAM. Same checkpoint format as linux_ipi_probe.
$(BUILD)/linux_csd_probe.out: $(HARNESS)/probes/linux_csd_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_csd_probe.cpp $(VSYS_LIB_FAST) -o $@

# Push/pop ledger for call_single_queue, watching the sc.d in llist_add_batch
# and the amoswap in __flush_smp_call_function_queue. Decides whether the lost
# cross-call entry was dropped before or after the dequeue.
$(BUILD)/linux_llist_probe.out: $(HARNESS)/probes/linux_llist_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_llist_probe.cpp $(VSYS_LIB_FAST) -o $@

# RTL-only Linux boot: no golden model, no run.log, just the Verilated core with
# its UART TX streamed to stdout (-DSHOW_TERMINAL). Links the FAST no-trace model
# (CXX_FAST sets -DCHIRON_NO_TRACE so rtl_model.h's tb->trace() compiles out) for
# the long Linux boot. STEP_TIMEOUT is raised (CXX_LINUX) so legitimate boot
# phases (bbl kernel copy, rootfs) aren't misclassified as hangs. The image is
# loaded by a direct memcpy into the Verilated DRAM (see rtl_model.h).
# Uses the same walker-ON RTL as CI (obj_dir_fast): walkerWriteBackBuffer keeps
# fence.i from deadlocking the snoop path under SMP, so a separate walker-off
# model is no longer required.
$(BUILD)/linux_sim.out: $(HARNESS)/linux_sim.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) -DSHOW_TERMINAL $(HARNESS)/linux_sim.cpp $(VSYS_LIB_FAST) -o $@

# Same boot, but watches for the known timekeeping-seqlock SMP wedge (see the
# file's own comment) and captures a bounded VCD right around the point it
# sets in, instead of either tracing from cycle 0 (far too slow/large) or not
# at all. Needs the TRACE-capable model (VCD dumping is unavailable on the
# fast no-trace build), so it is undumped-but-not-optimized-away until triggered.
$(BUILD)/linux_sim_trace.out: $(HARNESS)/linux_sim_trace.cpp $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/linux_sim_trace.cpp $(VSYS_LIB) -o $@

# Same wedge-capture idea as linux_sim_trace.out, but for the mt-seqlock
# microbenchmark (see workloads/benchmarks/mt-seqlock/) instead of a full
# Linux boot: any hart's PC frozen for --threshold cycles is by construction
# a wedge (no legitimate loop in that benchmark sits still that long).
$(BUILD)/seqlock_wedge_trace.out: $(HARNESS)/seqlock_wedge_trace.cpp $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/seqlock_wedge_trace.cpp $(VSYS_LIB) -o $@

$(BUILD)/profile.out: $(HARNESS)/profile.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_NOTRACE) $(HARNESS)/profile.cpp $(VSYS_LIB) -o $@

$(BUILD)/fire.out: $(HARNESS)/fire.cpp $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_NOTRACE) $(HARNESS)/fire.cpp $(VSYS_LIB) -o $@

$(BUILD)/profile_quad.out: $(HARNESS)/profile_quad.cpp $(SIM)/profiler_quad.h $(VSYS_LIB) | $(BUILD)
	$(CXX_NOTRACE) $(HARNESS)/profile_quad.cpp $(VSYS_LIB) -o $@

# Golden-model emulator, standalone (no RTL). Built WITHOUT -DLOCKSTEP so it
# uses the real timer path (fires only when mtime>=mtimecmp) and reads console
# input from stdin — both required to boot Linux. (The lock-step harnesses keep
# -DLOCKSTEP, which force-fires a timer interrupt every step for RTL sync.)
# Reads its image path from argv[1].
$(BUILD)/emu.out: $(EMU)/emulator_linux.cpp $(EMU_HDRS) | $(BUILD)
	g++ -O2 -I $(EMU) -o $@ $(EMU)/emulator_linux.cpp

# ── Runtime flag helpers ──────────────────────────────────────────────────────
# Expand to the appropriate CLI flag when the user passes SHOW_STATE=1 or
# DUMP_WAVES=1; expand to nothing otherwise.
# Wall-clock cap per benchmark in profile-all / profile-all-sc. A benchmark that
# exceeds it is killed mid-run and silently leaves no JSON, so the sweep looks
# like it succeeded with one result missing — keep this comfortably above the
# slowest benchmark (csaxpy-q4 is ~10 min on the traced model).
PROFILE_TIMEOUT ?= 1800

_SHOW_STATE_FLAG := $(if $(filter 1,$(SHOW_STATE)),--show-state,)
_DUMP_WAVES_FLAG := $(if $(filter 1,$(DUMP_WAVES)),--dump-waves,)

# ── Run targets — one entry point per task, no file copying ───────────────────
ISA_IMAGES := $(ISA_DIR)/images

.PHONY: emu lockstep profile profile-all profile-all-sc profile-quad test-q4 isa \
        fire cube solid test linux linux-emu linux-emu-check linux-sim \
        linux-lockstep demo compare snapshot-baseline gate regress-q4 \
        ci-bench ci-check

emu: $(BUILD)/emu.out                ## Run BENCH on the golden emulator (fast)
	$(BUILD)/emu.out $(BIN)

lockstep: $(BUILD)/lockstep.out      ## Lock-step RTL vs emulator for BENCH
	$(BUILD)/lockstep.out --image $(BIN) $(DONE) --logdir $(BUILD) \
	    $(_SHOW_STATE_FLAG) $(_DUMP_WAVES_FLAG)

# DEBUG=1 sends the harness's own progress to a log file instead of discarding
# it; stdout stays the benchmark's UART output plus the final verdict. No
# checkpoints here -- these runs are minutes long, so there is nothing to
# resume from (see linux-sim for the case that needs it).
DEBUG_FLAGS := $(if $(DEBUG),--debug --log $(BUILD)/$(if $(BENCH),$(BENCH),profile)-debug.log)

profile: $(BUILD)/profile.out        ## Cycle-accurate profile (IPC) for BENCH
	@mkdir -p $(BUILD)/profile_results
	@echo "[profile] $(BENCH)" >&2
	$(BUILD)/profile.out --image $(BIN) --name $(BENCH) $(DONE) $(DEBUG_FLAGS) \
		--output $(BUILD)/profile_results/$(BENCH).json --timeout 100000000

profile-quad: $(BUILD)/profile_quad.out    ## Quad-core profile (IPC) for FAM (e.g. make profile-quad FAM=vvadd)
	@mkdir -p $(BUILD)/profile_results
	@echo "[profile-quad] $(FAM)-q4" >&2
	$(BUILD)/profile_quad.out \
	    --image $(BINS)/$($(FAM)_base)-q4.bin \
	    --name $(FAM)-q4 $($(FAM)_DONE) \
	    $(if $(DEBUG),--debug --log $(BUILD)/$(FAM)-q4-debug.log) \
	    --output $(BUILD)/profile_results/$(FAM)-q4.json --timeout 100000000

profile-all: $(BUILD)/profile_quad.out    ## Profile all quad-core benchmarks (default: q4 bins)
	@mkdir -p $(BUILD)/profile_results
	$(foreach fam,$(BENCHES), \
	  echo "[profile-all] $(fam)-q4" && \
	  test -f $(BINS)/$($(fam)_base)-q4.bin && \
	  timeout $(PROFILE_TIMEOUT) $(BUILD)/profile_quad.out \
	    --image $(BINS)/$($(fam)_base)-q4.bin \
	    --name $(fam)-q4 $($(fam)_DONE) \
	    --output $(BUILD)/profile_results/$(fam)-q4.json --timeout 100000000 || exit 1; )
	python3 scripts/profile_visualize.py $(BUILD)/profile_results/

profile-all-sc: $(BUILD)/profile.out    ## Profile single-core (NUM_CORES=1) bins, all scales
	@mkdir -p $(BUILD)/profile_results
	$(foreach fam,$(BENCHES),$(foreach s,1 2 3 4 5, \
	  echo "[profile-all-sc] $(fam)-s$(s)" && \
	  test -f $(BINS)/$($(fam)_base)-s$(s).bin && \
	  timeout $(PROFILE_TIMEOUT) $(BUILD)/profile.out --image $(BINS)/$($(fam)_base)-s$(s).bin \
	    --name $(fam)-s$(s) $($(fam)_DONE) \
	    --output $(BUILD)/profile_results/$(fam)-s$(s).json --timeout 100000000 || exit 1; ))
	python3 scripts/profile_visualize.py $(BUILD)/profile_results/

isa: test_all_images                 ## Alias for the full ISA regression suite

fire: $(BUILD)/fire.out $(BINS)/mt-fire.bin   ## Render the bare-metal fire demo
	$(BUILD)/fire.out --image $(BINS)/mt-fire.bin --frames $(FIRE_FRAMES)
FIRE_FRAMES ?= 60

cube: $(BUILD)/fire.out $(BINS)/mt-cube.bin   ## Wireframe rotating cube (UART truecolor)
	$(BUILD)/fire.out --image $(BINS)/mt-cube.bin --frames $(FIRE_FRAMES)

solid: $(BUILD)/fire.out $(BINS)/mt-solid.bin ## Filled, shaded rotating cube
	$(BUILD)/fire.out --image $(BINS)/mt-solid.bin --frames $(FIRE_FRAMES)

test-q4: $(BUILD)/profile_quad.out   ## Pass/fail check for quad-core benchmarks (uses -q4 bins)
	@for fam in $(REGRESSION_Q4); do \
	  echo "== quad-core $$fam-q4 =="; \
	  $(MAKE) --no-print-directory profile-quad FAM=$$fam || exit 1; \
	  echo "$$fam-q4: PASS"; \
	done

BASELINE_Q4 := testdata/baseline/q4

compare:   ## Diff build/profile_results against testdata/baseline/q4
	python3 scripts/profile_compare.py $(BASELINE_Q4) $(BUILD)/profile_results

snapshot-baseline:   ## Copy current q4 JSON into testdata/baseline/q4 (commit the refresh)
	@mkdir -p $(BASELINE_Q4)
	@for fam in $(REGRESSION_Q4_ALL); do \
	  test -f $(BUILD)/profile_results/$$fam-q4.json || { echo "missing $$fam-q4.json"; exit 1; }; \
	  cp $(BUILD)/profile_results/$$fam-q4.json $(BASELINE_Q4)/; \
	  echo "updated $(BASELINE_Q4)/$$fam-q4.json"; \
	done

gate: $(BUILD)/lockstep.out $(BUILD)/profile_quad.out   ## Everyday RTL gate: lockstep vvadd-s1 + vvadd-q4 vs baseline
	@$(MAKE) --no-print-directory runLockStep
	@$(MAKE) --no-print-directory profile-quad FAM=vvadd
	python3 scripts/profile_compare.py $(BASELINE_Q4) $(BUILD)/profile_results --only vvadd-q4

regress-q4: $(BUILD)/profile_quad.out   ## All five q4 benches, then compare against the committed baseline
	@for fam in $(REGRESSION_Q4_ALL); do \
	  $(MAKE) --no-print-directory profile-quad FAM=$$fam || exit 1; \
	done
	python3 scripts/profile_compare.py $(BASELINE_Q4) $(BUILD)/profile_results

# Fast (no-trace) profile_quad -- much quicker on the large s5 datasets than the
# traced model. Requires the fast RTL model (make sim-fast).
$(BUILD)/profile_quad_fast.out: $(HARNESS)/profile_quad.cpp $(SIM)/profiler_quad.h $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) -I $(SIM) $(HARNESS)/profile_quad.cpp $(VSYS_LIB_FAST) -o $@

# Same harness, built with the microarchitectural assertions in
# sim/harness/invariants.h compiled IN. Slower (it reads all 16 ROB ready bits
# per core per cycle), so it is a separate binary rather than a flag on the
# profiling one -- `make ci-bench` stays fast, `make ci-check` stays strict.
$(BUILD)/profile_quad_check.out: $(HARNESS)/profile_quad.cpp $(HARNESS)/invariants.h $(SIM)/profiler_quad.h $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) -DCHIRON_INVARIANTS -I $(SIM) $(HARNESS)/profile_quad.cpp $(VSYS_LIB_FAST) -o $@

# ── CI benchmark gate ─────────────────────────────────────────────────────────
# The five max-scale quad-core benchmarks that must ALL reach BENCHMARK COMPLETE
# for CI to pass. Runs the committed -q4 scale bins on the fast no-trace model.
# "BENCHMARK COMPLETE" (not "Simulation cycles", which prints on TIMEOUT too) is
# the success marker.
ci-bench: $(BUILD)/profile_quad_fast.out   ## CI gate: 5 quad-core benchmarks must all complete cleanly
	@fail=0; \
	run() { \
	  echo "== CI bench: $$1 =="; \
	  out=$$(timeout 1500 $(BUILD)/profile_quad_fast.out --image $(BINS)/$$2 --name $$1 $$3 \
	       --timeout 120000000 2>&1); \
	  echo "$$out" | tail -4; \
	  if echo "$$out" | grep -q 'BENCHMARK COMPLETE' && \
	     ! echo "$$out" | grep -qiE 'Register mismatch|Deadlock|TIMEOUT|VERIFY FAIL' && \
	     ! echo "$$out" | grep -E 'error code: [1-9]'; \
	  then echo "$$1: PASS"; echo "$$1: pass" >> test_results.txt; \
	  else echo "$$1: FAIL"; echo "$$1: fail" >> test_results.txt; fail=1; fi; }; \
	run vvadd-s5-q4  mt-vvadd-s5-q4.bin        "$(vvadd_DONE)"; \
	run matmul-s1-q4 mt-matmul-s1-q4.bin       "$(matmul_DONE)"; \
	run filter-s5-q4 mt-mask-sfilter-s5-q4.bin "$(filter_DONE)"; \
	run csaxpy-s5-q4 mt-csaxpy-s5-q4.bin       "$(csaxpy_DONE)"; \
	run histo-s5-q4  mt-histo-s5-q4.bin        "$(histo_DONE)"; \
	if [ $$fail -eq 0 ]; then echo "ci-bench: ALL 5 PASS"; else echo "ci-bench: FAILURES"; exit 1; fi

# ── CI correctness gate ───────────────────────────────────────────────────────
# ci-bench asks "did the benchmark produce the right answer?". This asks the
# stricter question: "did the microarchitecture stay self-consistent while it
# did?" Four separate wedges in this repo were caused by a completion landing on
# a ROB slot that speculation had already reallocated -- each corrupted state
# silently for millions of cycles before anything visibly hung, and each cost
# days of one-off probing to find. sim/harness/invariants.h asserts the
# invariants they violated on every cycle; a violation fails the run even if the
# benchmark still happened to compute the right result.
ci-check: $(BUILD)/profile_quad_check.out   ## Strict gate: benchmarks + per-cycle microarchitectural assertions
	@fail=0; \
	run() { \
	  echo "== CI check: $$1 =="; \
	  out=$$(timeout 3000 $(BUILD)/profile_quad_check.out --image $(BINS)/$$2 --name $$1 $$3 \
	       --timeout 120000000 2>&1); rc=$$?; \
	  echo "$$out" | grep -E 'INVARIANT|chiron invariants|no violations|VIOLATION|BENCHMARK COMPLETE|TIMEOUT|DEADLOCK' || true; \
	  if [ $$rc -eq 0 ]; then echo "$$1: CLEAN"; \
	  else echo "$$1: FAIL (rc=$$rc: 2=timeout 3=deadlock 4=invariant-violation 124=wall-clock)"; fail=1; fi; }; \
	run vvadd-s5-q4  mt-vvadd-s5-q4.bin        "$(vvadd_DONE)"; \
	run matmul-s1-q4 mt-matmul-s1-q4.bin       "$(matmul_DONE)"; \
	run filter-s5-q4 mt-mask-sfilter-s5-q4.bin "$(filter_DONE)"; \
	run csaxpy-s5-q4 mt-csaxpy-s5-q4.bin       "$(csaxpy_DONE)"; \
	run histo-s5-q4  mt-histo-s5-q4.bin        "$(histo_DONE)"; \
	if [ $$fail -eq 0 ]; then echo "ci-check: ALL 5 CLEAN"; else echo "ci-check: FAILURES"; exit 1; fi

# ── Constrained-random stress campaign ────────────────────────────────────────
# The generated program's <exit> address moves with every seed and block count,
# so the done-pc is derived from the freshly built dump rather than pinned in a
# variable the way the fixed benchmarks are. Link base is 0; the RTL runs at
# 0x8000_0000.
STRESS_TIMEOUT ?= 20000000
STRESS_SEEDS   ?= 1 2 3 4 5 6 7 8

.PHONY: stress-run stress-sweep
stress-run: $(BUILD)/profile_quad_check.out   ## Run the current bins/mt-stress-q4.bin under the invariant assertions
	@pc=$$(grep -m1 '<exit>:' $(BENCH_SRC)/mt-stress.riscv.dump | cut -d' ' -f1); \
	 pc=$$(printf '0x%x' $$((0x$$pc + 0x80000000))); \
	 echo "[stress-run] seed=$(SEED) blocks=$(BLOCKS) done-pc=$$pc"; \
	 $(BUILD)/profile_quad_check.out --image $(BINS)/mt-stress-q4.bin \
	     --name stress-q4 --done-pc $$pc --timeout $(STRESS_TIMEOUT)

# One seed per iteration: regenerate, rebuild, run, and stop at the first seed
# that violates an invariant or fails to complete -- that seed is the repro, and
# `make stress-bin SEED=<n> && make stress-run SEED=<n>` replays it exactly.
stress-sweep: $(BUILD)/profile_quad_check.out   ## Sweep STRESS_SEEDS; stops at the first failing seed
	@for s in $(STRESS_SEEDS); do \
	  echo "===== stress seed $$s ====="; \
	  $(MAKE) --no-print-directory stress-bin SEED=$$s BLOCKS=$(BLOCKS) > /dev/null || exit 1; \
	  out=$$($(MAKE) --no-print-directory stress-run SEED=$$s BLOCKS=$(BLOCKS) 2>&1); rc=$$?; \
	  echo "$$out" | grep -E 'INVARIANT|VIOLATION|no violations|BENCHMARK COMPLETE|TIMEOUT|DEADLOCK' || true; \
	  if [ $$rc -eq 0 ]; then \
	    echo "seed $$s: CLEAN"; \
	  else \
	    echo "seed $$s: FAIL (rc=$$rc)  -- replay with: make stress-bin SEED=$$s BLOCKS=$(BLOCKS) && make stress-run SEED=$$s"; \
	    exit 1; \
	  fi; \
	done; \
	echo "stress-sweep: all seeds clean"

# ── Performance dashboard ─────────────────────────────────────────────────────
# profile_quad already emits every counter worth plotting, so this is pure
# post-processing: run the benchmarks with --output, then render one
# self-contained HTML file (no external libraries, opens anywhere).
PROFILE_DIR ?= $(BUILD)/profiles
DASHBOARD   ?= $(BUILD)/chiron_dashboard.html

.PHONY: profiles dashboard
profiles: $(BUILD)/profile_quad_fast.out   ## Run the 5 benchmarks, writing profiler JSON to PROFILE_DIR
	@mkdir -p $(PROFILE_DIR)
	@run() { \
	  echo "== profiling $$1 =="; \
	  timeout 3000 $(BUILD)/profile_quad_fast.out --image $(BINS)/$$2 --name $$1 $$3 \
	      --timeout 120000000 --output $(PROFILE_DIR)/$$1.json > $(PROFILE_DIR)/$$1.txt 2>&1 \
	    || echo "  $$1: FAILED (rc=$$?) — see $(PROFILE_DIR)/$$1.txt"; }; \
	run vvadd-s5-q4  mt-vvadd-s5-q4.bin        "$(vvadd_DONE)"; \
	run matmul-s1-q4 mt-matmul-s1-q4.bin       "$(matmul_DONE)"; \
	run filter-s5-q4 mt-mask-sfilter-s5-q4.bin "$(filter_DONE)"; \
	run csaxpy-s5-q4 mt-csaxpy-s5-q4.bin       "$(csaxpy_DONE)"; \
	run histo-s5-q4  mt-histo-s5-q4.bin        "$(histo_DONE)"

dashboard:   ## Render PROFILE_DIR's JSON into a self-contained HTML dashboard
	@python3 tools/viz_report.py --profiles $(PROFILE_DIR) --out $(DASHBOARD)

test: isa test-q4                    ## ISA suite + quad-core benchmark tests

# ── Linux image build (mc-linux/ submodule) ──────────────────────
IMG_DIR := mc-linux

.PHONY: patch linux-toolchain linux-image-s1 linux-image-q4 linux-images

patch:   ## Update linux/buildroot/riscv-pk submodules + stage chiron patches
	cd $(IMG_DIR) && ./submodule_update && ./apply_configs_and_patches

linux-toolchain: patch   ## Build the buildroot cross toolchain + rootfs (slow, once)
	$(MAKE) -C $(IMG_DIR)/buildroot -j$(shell nproc)

linux-image-s1: patch   ## Build bins/linux-s1.bin (single-core nommu Linux)
	cd $(IMG_DIR) && RISCV="$$PWD/buildroot/output/host" ./build_image.sh s1 ../$(BINS)

linux-image-q4: patch   ## Build bins/linux-q4.bin (quad-core SMP Linux)
	cd $(IMG_DIR) && RISCV="$$PWD/buildroot/output/host" ./build_image.sh q4 ../$(BINS)

linux-images: linux-image-s1 linux-image-q4   ## Build both Linux images

# ── Linux boot (nommu RISC-V image, see mc-linux/) ───────────────
# LINUX_IMAGE selects the bbl.bin to run; override on the command line, e.g.
#   make linux-emu LINUX_IMAGE=bins/linux-q4.bin
LINUX_IMAGE ?= $(BINS)/linux-q4.bin

linux-emu: $(BUILD)/emu.out          ## Interactive Linux shell on the golden model (fast)
	@echo "== interactive golden-model boot: $(LINUX_IMAGE) =="
	@echo "   (boots to 'buildroot login:' in seconds — type at the prompt; Ctrl-C to quit)"
	$(BUILD)/emu.out $(LINUX_IMAGE)

linux-emu-check: $(BUILD)/emu.out    ## Non-interactive boot-to-login check (CI)
	@scripts/run_linux.sh emu $(LINUX_IMAGE) $(if $(TIMEOUT),$(TIMEOUT),300)

# ── linux-sim: the RTL boot ───────────────────────────────────────────────────
# Its stdout is the guest console and nothing else, so `make linux-sim | tee
# boot.log` gives a log that is purely what Linux printed. Everything the
# harness has to say is opt-in:
#
#   make linux-sim                  quiet: guest output only
#   make linux-sim DEBUG=1          + harness log ($(LINUX_SIM_LOG)) and
#                                     checkpoints every $(CKPT_EVERY) cycles
#   make linux-sim DEBUG=1 RESUME=1 resume from the newest checkpoint
#   make linux-sim RESTORE=<file>   resume from a specific one
#   make linux-ckpts                list what is available to resume from
#
# Checkpoints are ~256 MB each (the model includes all of DRAM), which is why
# CKPT_KEEP prunes old ones and why none are written unless DEBUG=1.
LINUX_SIM_LOG ?= $(BUILD)/linux-sim.log
CKPT_DIR      ?= ckpt
CKPT_EVERY    ?= 20000000
CKPT_KEEP     ?= 8

LINUX_SIM_FLAGS := $(if $(DEBUG),--debug --log $(LINUX_SIM_LOG) \
                     --ckpt-dir $(CKPT_DIR) --ckpt-every $(CKPT_EVERY) \
                     --ckpt-keep $(CKPT_KEEP))
LINUX_SIM_FLAGS += $(if $(RESTORE),--restore $(RESTORE))

linux-sim: $(BUILD)/linux_sim.out    ## Boot LINUX_IMAGE on the RTL core (guest console only; DEBUG=1 for logs+checkpoints)
	@echo "== RTL boot: $(LINUX_IMAGE) (Verilator ~thousands of cyc/s) ==" >&2
	@$(if $(DEBUG),echo "   debug log: $(LINUX_SIM_LOG)   checkpoints: $(CKPT_DIR)/ every $(CKPT_EVERY) cyc" >&2,\
	   echo "   (quiet: only what the kernel transmits. DEBUG=1 adds a log + checkpoints)" >&2)
	@flags="$(LINUX_SIM_FLAGS)"; \
	if [ -n "$(RESUME)" ] && [ -z "$(RESTORE)" ]; then \
	  ck=$$(ls -1 $(CKPT_DIR)/ckpt_*.bin 2>/dev/null | sort | tail -1); \
	  if [ -z "$$ck" ]; then \
	    echo "no checkpoints in $(CKPT_DIR)/ — run once with DEBUG=1 first" >&2; exit 1; \
	  fi; \
	  echo "   resuming from $$ck" >&2; \
	  flags="$$flags --restore $$ck"; \
	fi; \
	exec $(BUILD)/linux_sim.out $(LINUX_IMAGE) $(DATA)/qemu.dtb $(DATA)/boot.bin $$flags

.PHONY: linux-ckpts
linux-ckpts:                         ## List checkpoints available to resume from
	@ls -lh $(CKPT_DIR)/ckpt_*.bin 2>/dev/null || echo "no checkpoints in $(CKPT_DIR)/"

linux-lockstep: $(BUILD)/lockstep_linux.out  ## Bounded RTL lock-step of LINUX_IMAGE (debug; slow)
	@scripts/run_linux.sh lockstep $(LINUX_IMAGE) $(if $(TIMEOUT),$(TIMEOUT),180)

# Back-compat alias: the old `linux` target now runs the golden-model boot.
linux: linux-emu                     ## Alias for linux-emu

demo: $(BUILD)/lockstep.out          ## Image-processing demo (mt-image.bin)
	$(BUILD)/lockstep.out --image $(BINS)/mt-image.bin --logdir $(BUILD)

# ── CI-compatible aliases (do not rename; .github/workflows depends on these) ─
.PHONY: runLockStep test_all_images
runLockStep: $(BUILD)/lockstep.out   ## CI: quick single lock-step (vvadd-s1) -- HARD GATE
	@rm -f run.log test_results.txt lockstep_smoke.log
	@set +e; $(BUILD)/lockstep.out --image $(BINS)/mt-vvadd-s1.bin $(vvadd_DONE) --logdir . > lockstep_smoke.log 2>&1; rc=$$?; set -e; \
	 cat lockstep_smoke.log; \
	 if [ $$rc -eq 0 ] && ! grep -qiE "Register mismatch|Test failed|Time-out|Deadlock" lockstep_smoke.log; then \
	   echo "vvadd-s1: pass" >> test_results.txt; echo "runLockStep: PASS"; \
	 else \
	   echo "vvadd-s1: fail" >> test_results.txt; \
	   echo "runLockStep: FAILED (register mismatch, timeout, or nonzero exit)"; exit 1; \
	 fi

test_all_images: $(BUILD)/lockstep_isa.out   ## CI: lock-step every ISA test image
	@rm -f test_results.txt
	@for img in $(ISA_IMAGES)/*; do \
	  name=$$(basename $$img); \
	  printf "[isa] %-42s " "$$name"; \
	  if $(BUILD)/lockstep_isa.out --image $$img >/dev/null 2>&1; then \
	    printf "pass\n"; echo "$$name: pass" >> test_results.txt; \
	  else \
	    printf "FAIL\n"; echo "$$name: fail" >> test_results.txt; \
	  fi; \
	done
	@PASSED=$$(grep -c ': pass' test_results.txt); \
	 TOTAL=$$(wc -l < test_results.txt); \
	 echo "ISA passed: $$PASSED / $$TOTAL"; \
	 [ $$PASSED -eq $$TOTAL ] || { echo "REGRESSION: $$PASSED/$$TOTAL passed (expected ALL to pass -- any FAIL, incl. fence_i, fails CI)"; grep -i ': fail' test_results.txt || true; exit 1; }

$(BUILD)/lrsc_wedge_probe.out: $(HARNESS)/probes/lrsc_wedge_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lrsc_wedge_probe2.out: $(HARNESS)/probes/lrsc_wedge_probe2.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe2.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lrsc_wedge_probe3.out: $(HARNESS)/probes/lrsc_wedge_probe3.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe3.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lrsc_wedge_probe4.out: $(HARNESS)/probes/lrsc_wedge_probe4.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe4.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lrsc_wedge_probe5.out: $(HARNESS)/probes/lrsc_wedge_probe5.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe5.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lrsc_wedge_probe6.out: $(HARNESS)/probes/lrsc_wedge_probe6.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe6.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lrsc_wedge_probe7.out: $(HARNESS)/probes/lrsc_wedge_probe7.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lrsc_wedge_probe7.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/rename_a4_probe.out: $(HARNESS)/probes/rename_a4_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/rename_a4_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/mshr_prfdest_probe.out: $(HARNESS)/probes/mshr_prfdest_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/mshr_prfdest_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/prf33_alloc_probe.out: $(HARNESS)/probes/prf33_alloc_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/prf33_alloc_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/dualalloc_probe.out: $(HARNESS)/probes/dualalloc_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/dualalloc_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/prfwrite_probe.out: $(HARNESS)/probes/prfwrite_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/prfwrite_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/w3path_probe.out: $(HARNESS)/probes/w3path_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/w3path_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/squashmiss_probe.out: $(HARNESS)/probes/squashmiss_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/squashmiss_probe.cpp $(VSYS_LIB_FAST) -o $@

$(BUILD)/lifetime_probe.out: $(HARNESS)/probes/lifetime_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/lifetime_probe.cpp $(VSYS_LIB_FAST) -o $@

# Watches ONE D-cache line's tag state (valid/dirty/shared) plus the fence.i
# walker FSM and both writeback staging slots, through the cycle window where
# csd_unlock's committed store goes missing. See linux_llist_probe for how that
# window was located.
$(BUILD)/linux_dcache_probe.out: $(HARNESS)/probes/linux_dcache_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_dcache_probe.cpp $(VSYS_LIB_FAST) -o $@

# Follows ONE line (the rt_sigreturn trampoline) through every place a stale
# copy can hide at once: hart1's I-cache line and its refill data, all four
# harts' D-cache copies, and every hart's fence.i walker sweep. Answers why an
# I-fetch returned zeros for an address whose data read was correct.
$(BUILD)/linux_tramp_probe.out: $(HARNESS)/probes/linux_tramp_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_tramp_probe.cpp $(VSYS_LIB_FAST) -o $@

# Watches ACEUnit's snoop-answer path for one line: does writePipeHit steer a
# peer's snoop into the writeback pipeline and serve the pre-store copy? Run on
# the PRE-FIX model, where the bug is live and the checkpoints still restore.
$(BUILD)/linux_snoop_probe.out: $(HARNESS)/probes/linux_snoop_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_snoop_probe.cpp $(VSYS_LIB_FAST) -o $@

# Restores a linux_ipi_probe checkpoint and traces the interrupt-injection FSM
# (core.scala waitForMTIP/waitToInjectInterr) for whichever hart has an
# unserviced MSIP pending — see inject_fsm_probe.cpp's header for the known
# branchCounter-stuck failure mode this was built to catch.
$(BUILD)/linux_injfsm_probe.out: $(HARNESS)/probes/linux_injfsm_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_injfsm_probe.cpp $(VSYS_LIB_FAST) -o $@

# Tests whether the Linux 1,235M freeze is a jammed ROB head with a lost branch
# resolution (the reallocated-slot defect class, task #40). See the probe's
# header: the interrupt-injection FSM was proven to be a symptom, not the cause.
$(BUILD)/linux_robjam_probe.out: $(HARNESS)/probes/linux_robjam_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_robjam_probe.cpp $(VSYS_LIB_FAST) -o $@

# Reads DRAM straight out of a checkpoint (no simulation, runs in seconds) to
# ask whether the address hart0 fetched zeros from actually contains code.
$(BUILD)/linux_itext_probe.out: $(HARNESS)/probes/linux_itext_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_LINUX) $(HARNESS)/probes/linux_itext_probe.cpp $(VSYS_LIB_FAST) -o $@

# Console RX round trip: drives the same hostInput pins linux_sim.cpp drives,
# against bins/mt-uartrx-q4.bin, and checks the bytes come back intact.
$(BUILD)/uartrx_test.out: $(HARNESS)/uartrx_test.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/uartrx_test.cpp $(VSYS_LIB_FAST) -o $@

.PHONY: uartrx-test
uartrx-test: $(BUILD)/uartrx_test.out   ## Verify the console RX path end to end
	$(BUILD)/uartrx_test.out

# ── SMP repro gate: mt-illegal + both mt-icoh variants ───────────────────────
# These three are NOT in benchmarks.mk's *_DONE table, and running them without
# a completion criterion looks exactly like a hang: common/syscalls.c's
# exit(int code) is just `while(1);`, so there is no done signal, the exit code
# stays in a0, and only hart0 ever exits (harts 1-3 spin by design) — so waiting
# on all cores times out by construction and a passing run reads as rc=124.
# Completion is therefore "--done-pc <exit> --done-a0 0", and the exit address
# is derived per build because mt-icoh's -D flags change code size (SELF=0 and
# SELF=1 land on different addresses; a hardcoded constant silently never
# matches).
.PHONY: smp-repro
smp-repro: $(BUILD)/profile_quad_fast.out  ## mt-illegal + mt-icoh (cross & self)
	@fail=0; \
	run() { \
	  pc=$$($(RISCV_BIN)/riscv64-unknown-elf-nm $$2 | awk '$$3=="exit"{print $$1}'); \
	  pc=$$(printf "0x%x" $$((0x80000000 + 0x$$pc))); \
	  echo "== $$1 (exit @ $$pc) =="; \
	  out=$$(timeout 900 $(BUILD)/profile_quad_fast.out --image $$3 --name $$1 \
	        --done-pc $$pc --done-a0 0 --timeout 120000000 2>&1); \
	  echo "$$out" | grep -E 'Simulation cycles' || true; \
	  if echo "$$out" | grep -q 'BENCHMARK COMPLETE' && \
	     ! echo "$$out" | grep -qiE 'Deadlock|TIMEOUT'; \
	  then echo "$$1: PASS"; else echo "$$1: FAIL"; fail=1; fi; }; \
	$(MAKE) illegal-bin >/dev/null; \
	run mt-illegal $(BENCH_SRC)/mt-illegal.riscv $(BINS)/mt-illegal-q4.bin; \
	$(MAKE) icoh-bin ICOH_SELF=0 >/dev/null; \
	run mt-icoh-cross $(BENCH_SRC)/mt-icoh.riscv $(BINS)/mt-icoh-q4.bin; \
	$(MAKE) icoh-bin ICOH_SELF=1 >/dev/null; \
	run mt-icoh-self $(BENCH_SRC)/mt-icoh.riscv $(BINS)/mt-icoh-q4.bin; \
	if [ $$fail -eq 0 ]; then echo "smp-repro: ALL 3 PASS"; else echo "smp-repro: FAILURES"; exit 1; fi

# Image-load integrity: streams an image through the programmer port and diffs
# all of DRAM against the file. Catches a dropped/mis-addressed load word, which
# otherwise shows up only as a wrong benchmark answer millions of cycles later.
$(BUILD)/image_load_probe.out: $(HARNESS)/probes/image_load_probe.cpp $(SIM_HDR) $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(HARNESS)/probes/image_load_probe.cpp $(VSYS_LIB_FAST) -o $@

.PHONY: image-check
image-check: $(BUILD)/image_load_probe.out  ## Verify an image lands in DRAM intact (IMG=bins/x.bin)
	$(BUILD)/image_load_probe.out $(if $(IMG),$(IMG),$(BINS)/mt-mask-sfilter-s5.bin)

# ── Full profiling sweep (the data behind docs/profile_report.png) ────────────
# Every family at every scale it has a dataset for, single-core AND quad-core,
# on the no-trace model. Runs PSWEEP_JOBS at a time: each simulation is
# single-threaded, so the sweep is embarrassingly parallel and goes from hours
# to tens of minutes on a multi-core host.
PSWEEP_DIR  ?= $(BUILD)/profile_results
PSWEEP_JOBS ?= $(shell nproc)

$(BUILD)/profile_fast.out: $(HARNESS)/profile.cpp $(SIM)/profiler.h $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) -I $(SIM) $(HARNESS)/profile.cpp $(VSYS_LIB_FAST) -o $@

.PHONY: profile-sweep profile-report
profile-sweep: $(BUILD)/profile_fast.out $(BUILD)/profile_quad_fast.out  ## Profile every family/scale, single + quad
	@mkdir -p $(PSWEEP_DIR)
	@echo "[sweep] $(PSWEEP_JOBS) jobs in parallel -> $(PSWEEP_DIR)"
	@: $(foreach f,$(SCALE_FAMILIES), \
	   $(foreach s,1 2 3 4 5, \
	     $(if $(shell test $(s) -le $(word 3,$(subst :, ,$(f))) && echo y), \
	       $(eval FAM := $(word 1,$(subst :, ,$(f)))) \
	       $(eval DIR := $(word 2,$(subst :, ,$(f)))) \
	       $(file >>$(BUILD)/.sweep.cmds,$(BUILD)/profile_fast.out --image $(BINS)/$(DIR)-s$(s).bin --name $(FAM)-s$(s) $($(FAM)_DONE) --output $(PSWEEP_DIR)/$(FAM)-s$(s).json --timeout 400000000 > $(BUILD)/.sweep-$(FAM)-s$(s).log 2>&1) \
	       $(file >>$(BUILD)/.sweep.cmds,$(BUILD)/profile_quad_fast.out --image $(BINS)/$(DIR)-s$(s)-q4.bin --name $(FAM)-s$(s)-q4 $($(FAM)_DONE) --output $(PSWEEP_DIR)/$(FAM)-s$(s)-q4.json --timeout 400000000 > $(BUILD)/.sweep-$(FAM)-s$(s)-q4.log 2>&1))))
	@wc -l < $(BUILD)/.sweep.cmds | xargs echo "[sweep] runs:"
	@xargs -a $(BUILD)/.sweep.cmds -d '\n' -P $(PSWEEP_JOBS) -I{} sh -c '{}' ; rm -f $(BUILD)/.sweep.cmds
	@echo "[sweep] done -> $(PSWEEP_DIR)"

profile-report: ## Render docs/profile_report.png from $(PSWEEP_DIR)
	python3 scripts/profile_visualize.py $(PSWEEP_DIR) --out docs/profile_report.png

.PHONY: done-pcs
done-pcs:   ## Print each benchmark's current exit PC (the *_DONE table in mk/benchmarks.mk)
	@for f in $(SCALE_FAMILIES); do \
	  fam=$${f%%:*}; rest=$${f#*:}; dir=$${rest%%:*}; \
	  $(TOOLPATH) $(MAKE) -s -C $(BENCH_SRC) riscv bmarks="$$dir" \
	     RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)" >/dev/null 2>&1; \
	  pc=$$($(RISCV_BIN)/riscv64-unknown-elf-nm $(BENCH_SRC)/$$dir.riscv 2>/dev/null \
	        | awk '$$3=="exit"{print $$1}'); \
	  [ -n "$$pc" ] && printf "%-10s exit = 0x%08x\n" "$$fam" $$((0x80000000 + 0x$$pc)); \
	done
