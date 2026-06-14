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

# Golden-model emulator, standalone (no RTL). Needs -DLOCKSTEP for the
# hart_set_interrupts overload; reads its image path from argv[1].
$(BUILD)/emu.out: $(EMU)/src/emulator_linux.cpp $(EMU)/src/emulator.h | $(BUILD)
	g++ -O2 -DLOCKSTEP -I $(EMU)/src -o $@ $(EMU)/src/emulator_linux.cpp

# ── Run targets — one entry point per task, no file copying ───────────────────
ISA_IMAGES := $(EMU)/riscv-tests/images

.PHONY: emu lockstep profile profile-all isa fire test linux demo

emu: $(BUILD)/emu.out                ## Run BENCH on the golden emulator (fast)
	$(BUILD)/emu.out $(BIN)

lockstep: $(BUILD)/lockstep.out      ## Lock-step RTL vs emulator for BENCH
	$(BUILD)/lockstep.out --image $(BIN) $(DONE) --logdir $(BUILD)

profile: $(BUILD)/profile.out        ## Cycle-accurate profile (IPC) for BENCH
	@mkdir -p $(BUILD)/profile_results
	$(BUILD)/profile.out --image $(BIN) --name $(BENCH) $(DONE) \
		--output $(BUILD)/profile_results/$(BENCH).json --timeout 500000000

profile-all: $(BUILD)/profile.out    ## Profile every manifest benchmark, all scales
	@mkdir -p $(BUILD)/profile_results
	$(foreach fam,$(BENCHES),$(foreach s,1 2 3 4 5, \
	  test -f $(BINS)/$($(fam)_base)-s$(s).bin && \
	  $(BUILD)/profile.out --image $(BINS)/$($(fam)_base)-s$(s).bin \
	    --name $(fam)-s$(s) $($(fam)_DONE) \
	    --output $(BUILD)/profile_results/$(fam)-s$(s).json --timeout 500000000 ; ))
	python3 scripts/profile_visualize.py $(BUILD)/profile_results/

isa: test_all_images                 ## Alias for the full ISA regression suite

fire: $(BUILD)/fire.out $(BINS)/mt-fire.bin   ## Render the bare-metal fire demo
	$(BUILD)/fire.out --image $(BINS)/mt-fire.bin --frames $(FIRE_FRAMES)
FIRE_FRAMES ?= 60

test: isa                            ## ISA suite + every benchmark, via lock-step
	@for b in $(REGRESSION); do \
	  echo "== lockstep $$b =="; \
	  $(MAKE) --no-print-directory lockstep BENCH=$$b || exit 1; \
	done

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
	  if $(BUILD)/lockstep_isa.out --image $$img >/dev/null 2>&1; then \
	    echo "$$img: pass" >> test_results.txt; \
	  else echo "$$img: fail" >> test_results.txt; fi; \
	done
	@echo "ISA passed: $$(grep -c ': pass' test_results.txt) / $$(wc -l < test_results.txt)"
