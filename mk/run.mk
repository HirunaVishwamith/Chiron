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
_SHOW_STATE_FLAG := $(if $(filter 1,$(SHOW_STATE)),--show-state,)
_DUMP_WAVES_FLAG := $(if $(filter 1,$(DUMP_WAVES)),--dump-waves,)

# ── Run targets — one entry point per task, no file copying ───────────────────
ISA_IMAGES := $(ISA_DIR)/images

.PHONY: emu lockstep profile profile-all profile-all-sc profile-quad test-q4 isa fire test linux linux-emu linux-emu-check linux-sim linux-lockstep demo

emu: $(BUILD)/emu.out                ## Run BENCH on the golden emulator (fast)
	$(BUILD)/emu.out $(BIN)

lockstep: $(BUILD)/lockstep.out      ## Lock-step RTL vs emulator for BENCH
	$(BUILD)/lockstep.out --image $(BIN) $(DONE) --logdir $(BUILD) \
	    $(_SHOW_STATE_FLAG) $(_DUMP_WAVES_FLAG)

profile: $(BUILD)/profile.out        ## Cycle-accurate profile (IPC) for BENCH
	@mkdir -p $(BUILD)/profile_results
	@echo "[profile] $(BENCH)"
	$(BUILD)/profile.out --image $(BIN) --name $(BENCH) $(DONE) \
		--output $(BUILD)/profile_results/$(BENCH).json --timeout 100000000

profile-quad: $(BUILD)/profile_quad.out    ## Quad-core profile (IPC) for FAM (e.g. make profile-quad FAM=vvadd)
	@mkdir -p $(BUILD)/profile_results
	@echo "[profile-quad] $(FAM)-q4"
	$(BUILD)/profile_quad.out \
	    --image $(BINS)/$($(FAM)_base)-q4.bin \
	    --name $(FAM)-q4 $($(FAM)_DONE) \
	    --output $(BUILD)/profile_results/$(FAM)-q4.json --timeout 100000000

profile-all: $(BUILD)/profile_quad.out    ## Profile all quad-core benchmarks (default: q4 bins)
	@mkdir -p $(BUILD)/profile_results
	$(foreach fam,$(BENCHES), \
	  echo "[profile-all] $(fam)-q4" && \
	  test -f $(BINS)/$($(fam)_base)-q4.bin && \
	  timeout 600 $(BUILD)/profile_quad.out \
	    --image $(BINS)/$($(fam)_base)-q4.bin \
	    --name $(fam)-q4 $($(fam)_DONE) \
	    --output $(BUILD)/profile_results/$(fam)-q4.json --timeout 100000000 || true ;)
	python3 scripts/profile_visualize.py $(BUILD)/profile_results/

profile-all-sc: $(BUILD)/profile.out    ## Profile single-core (NUM_CORES=1) bins, all scales
	@mkdir -p $(BUILD)/profile_results
	$(foreach fam,$(BENCHES),$(foreach s,1 2 3 4 5, \
	  echo "[profile-all-sc] $(fam)-s$(s)" && \
	  test -f $(BINS)/$($(fam)_base)-s$(s).bin && \
	  timeout 600 $(BUILD)/profile.out --image $(BINS)/$($(fam)_base)-s$(s).bin \
	    --name $(fam)-s$(s) $($(fam)_DONE) \
	    --output $(BUILD)/profile_results/$(fam)-s$(s).json --timeout 100000000 || true ; ))
	python3 scripts/profile_visualize.py $(BUILD)/profile_results/

isa: test_all_images                 ## Alias for the full ISA regression suite

fire: $(BUILD)/fire.out $(BINS)/mt-fire.bin   ## Render the bare-metal fire demo
	$(BUILD)/fire.out --image $(BINS)/mt-fire.bin --frames $(FIRE_FRAMES)
FIRE_FRAMES ?= 60

test-q4: $(BUILD)/profile_quad.out   ## Pass/fail check for quad-core benchmarks (uses -q4 bins)
	@for fam in $(REGRESSION_Q4); do \
	  echo "== quad-core $$fam-q4 =="; \
	  $(MAKE) --no-print-directory profile-quad FAM=$$fam || exit 1; \
	  echo "$$fam-q4: PASS"; \
	done

# Fast (no-trace) profile_quad -- much quicker on the large s5 datasets than the
# traced model. Requires the fast RTL model (make sim-fast).
$(BUILD)/profile_quad_fast.out: $(HARNESS)/profile_quad.cpp $(SIM)/profiler_quad.h $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) -I $(SIM) $(HARNESS)/profile_quad.cpp $(VSYS_LIB_FAST) -o $@

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
	     ! echo "$$out" | grep -qiE 'Register mismatch|Deadlock|TIMEOUT'; \
	  then echo "$$1: PASS"; echo "$$1: pass" >> test_results.txt; \
	  else echo "$$1: FAIL"; echo "$$1: fail" >> test_results.txt; fail=1; fi; }; \
	run vvadd-s5-q4  mt-vvadd-s5-q4.bin        "$(vvadd_DONE)"; \
	run matmul-s1-q4 mt-matmul-s1-q4.bin       "$(matmul_DONE)"; \
	run filter-s5-q4 mt-mask-sfilter-s5-q4.bin "$(filter_DONE)"; \
	run csaxpy-s5-q4 mt-csaxpy-s5-q4.bin       "$(csaxpy_DONE)"; \
	run histo-s5-q4  mt-histo-s5-q4.bin        "$(histo_DONE)"; \
	if [ $$fail -eq 0 ]; then echo "ci-bench: ALL 5 PASS"; else echo "ci-bench: FAILURES"; exit 1; fi

test: isa test-q4                    ## ISA suite + quad-core benchmark tests

# ── Linux image build (Multicore_Linux_Image/ submodule) ──────────────────────
IMG_DIR := Multicore_Linux_Image

.PHONY: patch linux-toolchain linux-image-s1 linux-image-q4 linux-images

patch:   ## Update linux/buildroot/riscv-pk submodules + stage chiron patches
	cd $(IMG_DIR) && ./submodule_update && ./apply_configs_and_patches

linux-toolchain:   ## Build the buildroot cross toolchain + rootfs (slow, once)
	$(MAKE) -C $(IMG_DIR)/buildroot -j$(shell nproc)

linux-image-s1: patch   ## Build bins/linux-s1.bin (single-core nommu Linux)
	cd $(IMG_DIR) && RISCV="$$PWD/buildroot/output/host" ./build_image.sh s1 ../$(BINS)

linux-image-q4: patch   ## Build bins/linux-q4.bin (quad-core SMP Linux)
	cd $(IMG_DIR) && RISCV="$$PWD/buildroot/output/host" ./build_image.sh q4 ../$(BINS)

linux-images: linux-image-s1 linux-image-q4   ## Build both Linux images

# ── Linux boot (nommu RISC-V image, see Multicore_Linux_Image/) ───────────────
# LINUX_IMAGE selects the bbl.bin to run; override on the command line, e.g.
#   make linux-emu LINUX_IMAGE=bins/linux-q4.bin
LINUX_IMAGE ?= $(BINS)/linux-q4.bin

linux-emu: $(BUILD)/emu.out          ## Interactive Linux shell on the golden model (fast)
	@echo "== interactive golden-model boot: $(LINUX_IMAGE) =="
	@echo "   (boots to 'buildroot login:' in seconds — type at the prompt; Ctrl-C to quit)"
	$(BUILD)/emu.out $(LINUX_IMAGE)

linux-emu-check: $(BUILD)/emu.out    ## Non-interactive boot-to-login check (CI)
	@scripts/run_linux.sh emu $(LINUX_IMAGE) $(if $(TIMEOUT),$(TIMEOUT),300)

linux-sim: $(BUILD)/linux_sim.out    ## Boot LINUX_IMAGE on the RTL core (live console, no dump)
	@echo "== RTL boot: $(LINUX_IMAGE) (Verilator ~thousands of cyc/s; no input) =="
	$(BUILD)/linux_sim.out $(LINUX_IMAGE) $(DATA)/qemu.dtb $(DATA)/boot.bin

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
