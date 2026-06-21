# ── Harness binaries (compiled into $(BUILD)) ────────────────────────────────
EMU_HDRS := $(EMU)/src/emulator.h $(EMU)/src/constants.h
SIM_HDR  := $(SIM)/simulator.h

$(BUILD)/lockstep.out: $(HARNESS)/lockstep.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/lockstep.cpp $(VSYS_LIB) -o $@

$(BUILD)/lockstep_isa.out: $(HARNESS)/lockstep_isa.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/lockstep_isa.cpp $(VSYS_LIB) -o $@

$(BUILD)/lockstep_linux.out: $(HARNESS)/lockstep_linux.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_TRACE) $(HARNESS)/lockstep_linux.cpp $(VSYS_LIB) -o $@

$(BUILD)/profile.out: $(HARNESS)/profile.cpp $(EMU_HDRS) $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_NOTRACE) $(HARNESS)/profile.cpp $(VSYS_LIB) -o $@

$(BUILD)/fire.out: $(HARNESS)/fire.cpp $(SIM_HDR) $(VSYS_LIB) | $(BUILD)
	$(CXX_NOTRACE) $(HARNESS)/fire.cpp $(VSYS_LIB) -o $@

$(BUILD)/profile_quad.out: $(HARNESS)/profile_quad.cpp $(SIM)/profiler_quad.h $(VSYS_LIB) | $(BUILD)
	$(CXX_NOTRACE) $(HARNESS)/profile_quad.cpp $(VSYS_LIB) -o $@

# Golden-model emulator, standalone (no RTL). Needs -DLOCKSTEP for the
# hart_set_interrupts overload; reads its image path from argv[1].
$(BUILD)/emu.out: $(EMU)/src/emulator_linux.cpp $(EMU)/src/emulator.h | $(BUILD)
	g++ -O2 -DLOCKSTEP -I $(EMU)/src -o $@ $(EMU)/src/emulator_linux.cpp

# ── Runtime flag helpers ──────────────────────────────────────────────────────
# Expand to the appropriate CLI flag when the user passes SHOW_STATE=1 or
# DUMP_WAVES=1; expand to nothing otherwise.
_SHOW_STATE_FLAG := $(if $(filter 1,$(SHOW_STATE)),--show-state,)
_DUMP_WAVES_FLAG := $(if $(filter 1,$(DUMP_WAVES)),--dump-waves,)

# ── Run targets — one entry point per task, no file copying ───────────────────
ISA_IMAGES := $(EMU)/riscv-tests/images

.PHONY: emu lockstep profile profile-all profile-all-sc profile-quad test-q4 isa fire test linux demo

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

test: isa test-q4                    ## ISA suite + quad-core benchmark tests

linux: $(BUILD)/lockstep_linux.out   ## Linux-boot lock-step (dtb/bootrom harness)
	$(BUILD)/lockstep_linux.out --image $(BIN)

demo: $(BUILD)/lockstep.out          ## Image-processing demo (mt-image.bin)
	$(BUILD)/lockstep.out --image $(BINS)/mt-image.bin --logdir $(BUILD)

# ── CI-compatible aliases (do not rename; .github/workflows depends on these) ─
.PHONY: runLockStep test_all_images
runLockStep: $(BUILD)/lockstep.out   ## CI: quick single lock-step (vvadd-s1)
	@rm -f run.log test_results.txt
	@$(BUILD)/lockstep.out --image $(BINS)/mt-vvadd-s1.bin $(vvadd_DONE) --logdir . ; \
	if [ $$? -eq 0 ]; then echo "vvadd-s1: pass" >> test_results.txt; \
	else echo "vvadd-s1: fail" >> test_results.txt; fi

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
	@echo "ISA passed: $$(grep -c ': pass' test_results.txt) / $$(wc -l < test_results.txt)"
