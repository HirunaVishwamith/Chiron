# ── Kairos: schedule exploration ─────────────────────────────────────────────
#
# RTL simulation of a multicore is deterministic: it explores ONE interleaving
# per test program, no matter how many times it runs. Kairos restores the
# variation that silicon gets for free, by delaying harts under a reproducible
# policy. See sim/kairos/README.md for the design and the soundness argument.
#
#   make kairos                       build the tool
#   make kairos-gate                  prove the RTL hook is inert when unused
#   make kairos-smoke                 quick 4-schedule run on one SMP micro
#   make kairos-sweep                 compare policies across the SMP micros
#
# Ad-hoc use goes straight through the binary:
#   build/kairos.out run --image bins/mt-seqlock-q4.bin --done-pc 0x80000ab4 \
#       --policy windowed:4096 --runs 64 --shrink
#
# Kairos links the FAST (no-trace, threaded) model, like every other probe. If
# you rebuild the RTL, `make` the tool again before believing a result: the
# harness links Vsystem__ALL.a statically, so a stale binary silently runs the
# PREVIOUS model and looks completely normal while doing it.

KAIROS_SRC  := sim/kairos
KAIROS_HDRS := $(KAIROS_SRC)/dut.h $(KAIROS_SRC)/dut_chiron.h \
               $(KAIROS_SRC)/policy.h $(KAIROS_SRC)/coverage.h \
               $(KAIROS_SRC)/oracle.h $(KAIROS_SRC)/schedule.h \
               $(KAIROS_SRC)/shrink.h $(KAIROS_SRC)/campaign.h

$(BUILD)/kairos.out: $(KAIROS_SRC)/main_kairos.cpp $(KAIROS_HDRS) $(SIM_HDR) \
                     $(VSYS_LIB_FAST) | $(BUILD)
	$(CXX_FAST) $(KAIROS_SRC)/main_kairos.cpp $(VSYS_LIB_FAST) -o $@

# Self-tests for the design-independent half: policies, coverage, oracle,
# schedules, shrinker. Links NO Verilated model, so it builds and runs in
# seconds and can gate every edit. Every serious bug this tool has had was in
# its own reasoning rather than in the simulator, and a broken tool still emits
# confident output -- these are the properties its conclusions rest on.
$(BUILD)/kairos_test: $(KAIROS_SRC)/tests/test_kairos.cpp $(KAIROS_HDRS) | $(BUILD)
	g++ -O1 -std=c++14 -Wall -I . $< -o $@

.PHONY: kairos kairos-test kairos-gate kairos-smoke kairos-sweep kairos-figs

kairos: $(BUILD)/kairos.out   ## Build the schedule-exploration tool

kairos-test: $(BUILD)/kairos_test  ## Kairos self-tests (no RTL, ~1s)
	@$(BUILD)/kairos_test

# Default campaign parameters. Overridable on the command line.
KAIROS_RUNS     ?= 16
KAIROS_CYCLES   ?= 8000000
KAIROS_POLICY   ?= windowed:4096
KAIROS_POLICIES ?= deterministic,random:0.02,pct:3,windowed:4096
KAIROS_SEED     ?= 1
KAIROS_HANG     ?= 300000
KAIROS_TEST     ?= mt-seqlock

# Every target below passes --done-a0 0: that is what ci-smp treats as a pass,
# and without it a test that RAN and reported failure would score the same as
# one that passed.

# ── The gate that protects the Linux-booting core ────────────────────────────
# The RTL hook must be INERT when Kairos is not driving it. This runs the
# deterministic policy (stall mask always 0) and requires every SMP micro to
# pass exactly as it does under the ordinary regression. If this fails, the
# hook has changed behaviour and nothing else Kairos reports can be trusted.
kairos-gate: $(BUILD)/kairos.out $(BUILD)/kairos_test  ## Prove the scheduleStall hook is inert at rest
	@$(BUILD)/kairos_test || exit 1; \
	fail=0; \
	for t in $(CI_SMP_TESTS); do \
	  name=$${t%%:*}; rest=$${t#*:}; bin=$${rest%%:*}; pc=$${rest##*:}; \
	  [ -f "$(BINS)/$$bin" ] || { echo "SKIP $$name (no $(BINS)/$$bin)"; continue; }; \
	  out=$$($(BUILD)/kairos.out run --image $(BINS)/$$bin --done-pc $$pc --done-a0 0 \
	          --dtb $(DATA)/qemu.dtb --boot $(DATA)/boot.bin \
	          --policy deterministic --runs 1 --cycles $(KAIROS_CYCLES) \
	          --hang $(KAIROS_HANG) --quiet 2>&1); \
	  if echo "$$out" | grep -qE "FINDING|already fails"; then \
	    echo "FAIL $$name"; echo "$$out"; fail=1; \
	  else echo "PASS $$name"; fi; \
	done; \
	[ $$fail -eq 0 ] && echo "kairos-gate: hook is inert on all SMP micros" || exit 1

kairos-smoke: $(BUILD)/kairos.out  ## 4 schedules on one micro (KAIROS_TEST=)
	@t=$$(for e in $(CI_SMP_TESTS); do case $$e in $(KAIROS_TEST):*) echo $$e;; esac; done); \
	[ -n "$$t" ] || { echo "no such test '$(KAIROS_TEST)' in CI_SMP_TESTS"; exit 2; }; \
	rest=$${t#*:}; bin=$${rest%%:*}; pc=$${rest##*:}; \
	$(BUILD)/kairos.out run --image $(BINS)/$$bin --done-pc $$pc --done-a0 0 \
	    --dtb $(DATA)/qemu.dtb --boot $(DATA)/boot.bin \
	    --policy $(KAIROS_POLICY) --runs 4 --cycles $(KAIROS_CYCLES) \
	    --hang $(KAIROS_HANG) --seed $(KAIROS_SEED)

# Rebuild every manuscript figure from the measured campaign reports. Figures
# are generated, never hand-drawn: one that lives in a binary file drifts out of
# date the first time the design changes and nobody notices.
kairos-figs:  ## Regenerate the paper figures from build/kairos/*.json
	@python3 tools/kairos/figures.py --outdir paper/dac27/figures

kairos-sweep: $(BUILD)/kairos.out  ## Compare policies across every SMP micro
	@mkdir -p $(BUILD)/kairos; \
	for t in $(CI_SMP_TESTS); do \
	  name=$${t%%:*}; rest=$${t#*:}; bin=$${rest%%:*}; pc=$${rest##*:}; \
	  [ -f "$(BINS)/$$bin" ] || continue; \
	  echo "### $$name"; \
	  $(BUILD)/kairos.out sweep --image $(BINS)/$$bin --done-pc $$pc --done-a0 0 \
	      --dtb $(DATA)/qemu.dtb --boot $(DATA)/boot.bin \
	      --policies $(KAIROS_POLICIES) --runs $(KAIROS_RUNS) \
	      --cycles $(KAIROS_CYCLES) --hang $(KAIROS_HANG) \
	      --seed $(KAIROS_SEED) --shrink \
	      --json $(BUILD)/kairos/$$name.json || true; \
	done; \
	echo "reports in $(BUILD)/kairos/ — plot with tools/kairos/analyze.py"
