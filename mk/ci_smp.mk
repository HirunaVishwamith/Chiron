# ── SMP / coherence correctness gate (runs in GitHub CI) ─────────────────────
#
# ci-bench asks "did the benchmark compute the right answer?" and ci-check asks
# "did the microarchitecture stay self-consistent while it did?". Neither
# exercises the SMP primitives directly: cross-hart atomics, LR/SC under
# contention, I-cache coherence, and the illegal-instruction trap. Every one of
# those has hosted a real bug in this design, and until now the tests covering
# them (smp-repro, the coherence micros, mt-llist) only ever ran by hand -- so
# nothing stopped a commit that broke them.
#
# ── Why a generated table ────────────────────────────────────────────────────
# The GitHub workflow deliberately does not install riscv64-unknown-elf-gcc
# (see the comments in .github/workflows/generic_test.yaml), so like ci-bench
# these run from committed bins/. That makes the binary and its exit PC a pair
# that MUST stay in lockstep: rebuilding a test can move its exit symbol -- it
# has shifted by 0x0c before -- and a stale --done-pc turns the gate into a
# silent no-op that still reports PASS, which is worse than no gate at all.
#
# So CI_SMP_TESTS is GENERATED, never hand-edited. After touching any of these
# tests run:
#
#     make ci-smp-refresh          # rebuild bins + rewrite mk/ci_smp_done.mk
#     git add -f bins/mt-*.bin mk/ci_smp_done.mk
#
# ci-smp-refresh reads each exit PC from the freshly linked .riscv with nm, so
# the table cannot disagree with the bin sitting next to it. It also removes
# crt.o and syscalls.o before each build: those are shared by every test but
# only the test's own .o is cleaned, and since crt.S never changes make will
# happily relink whatever flags built it first -- crt.S bakes li a1,NUM_CORES,
# so a stale crt.o silently gives a bin the wrong hart count.
#
# ── Always drive these through make, never the raw binary ────────────────────
# build/profile_quad_fast.out links Vsystem__ALL.a statically. Running it
# directly after rebuilding the RTL executes the PREVIOUS model, and the run
# looks completely normal -- same output format, plausible cycle counts. That
# cost real time here: mt-llist was declared fixed on the strength of two runs
# that were actually exercising a model which had already been reverted. If you
# are comparing an RTL change, `make` the harness (or check that its mtime is
# newer than sim/rtl/obj_dir_fast/Vsystem__ALL.a) before believing a result.

include mk/ci_smp_done.mk

# Cycle cap per test. The slowest of these (mt-llist) completes in ~1.9M
# cycles. mt-llist's own no-progress watchdog trips around 7M cycles and
# reports "stalled=1" with a named failure, so this cap sits well above the
# watchdog: a starved run should describe itself rather than die as an
# anonymous timeout.
CI_SMP_CYCLES ?= 30000000
CI_SMP_WALL   ?= 900

.PHONY: ci-smp ci-smp-refresh

ci-smp: $(BUILD)/profile_quad_fast.out  ## SMP/coherence micros from committed bins (no toolchain needed)
	@fail=0; \
	for t in $(CI_SMP_TESTS); do \
	  name=$${t%%:*}; rest=$${t#*:}; bin=$${rest%%:*}; pc=$${rest##*:}; \
	  if [ ! -f "$(BINS)/$$bin" ]; then \
	    echo "::error::$$name: $(BINS)/$$bin is missing -- run 'make ci-smp-refresh' and commit the bin"; \
	    fail=1; continue; \
	  fi; \
	  echo "== $$name (exit @ $$pc) =="; \
	  out=$$(timeout $(CI_SMP_WALL) $(BUILD)/profile_quad_fast.out \
	         --image $(BINS)/$$bin --name $$name \
	         --done-pc $$pc --done-a0 0 --timeout $(CI_SMP_CYCLES) 2>&1); \
	  echo "$$out" | grep -E 'Simulation cycles' || true; \
	  if echo "$$out" | grep -q 'BENCHMARK COMPLETE' && \
	     ! echo "$$out" | grep -qiE 'Deadlock|TIMEOUT|: FAIL'; then \
	    echo "$$name: PASS"; \
	  else \
	    echo "::error::$$name failed"; \
	    echo "$$out" | tail -25; \
	    fail=1; \
	  fi; \
	done; \
	if [ $$fail -eq 0 ]; then \
	  echo "ci-smp: ALL PASS"; \
	else \
	  echo "ci-smp: FAILURES -- see above"; exit 1; \
	fi

# Rebuild every gated test in its CI configuration, stage the bin, and rewrite
# the done-PC table from the freshly linked ELF. Needs the RISC-V toolchain, so
# this is a developer command -- CI only ever consumes what it produces.
#
# Each entry is <target>:<make-args>:<staged-bin-name>. The build rules all
# write a fixed filename, so variants of one source (mt-icoh cross vs self) are
# copied to distinct names here rather than fighting over one.
#
# Tests named in CI_SMP_HOLD are still built and staged, but are left OUT of
# the generated CI_SMP_TESTS so they do not gate CI. Hold a test out only with
# a written reason, and only while it is genuinely unresolved.
#
#   mt-llist -- does not pass on current RTL. Its consumer no longer issues the
#   AMO storm that made it a spec-legal livelock (see the LIVENESS note in
#   mt-llist.c), yet three producers doing cmpxchg on one word still make no
#   progress while the consumer only reads. CAS lock-freedom says one of them
#   should win each round, so this now looks like an LR/SC forward-progress
#   weakness in the RTL rather than a testbench defect. Run it by hand with
#   `make llist-bin && build/profile_quad_fast.out --image bins/mt-llist-q4.bin`.
#   Do NOT re-add it to the gate until it passes on a harness that was relinked
#   against the RTL under test -- an earlier "pass" here was a stale
#   profile_quad_fast.out still linked to a previous model.
CI_SMP_HOLD := mt-llist

CI_SMP_BUILD := \
  llist-bin:PUSH_CAS=1:mt-llist:mt-llist-q4.bin \
  seqlock-bin::mt-seqlock:mt-seqlock-q4.bin \
  spinwait-bin::mt-spinwait:mt-spinwait-q4.bin \
  fencei-bin:FENCEI=1:mt-fencei:mt-fencei-q4.bin \
  crosscall-bin:CC_FENCEI=1:mt-crosscall:mt-crosscall-q4.bin \
  illegal-bin::mt-illegal:mt-illegal-q4.bin \
  icoh-bin:ICOH_SELF=0:mt-icoh:mt-icoh-cross-q4.bin \
  icoh-bin:ICOH_SELF=1:mt-icoh:mt-icoh-self-q4.bin

ci-smp-refresh:  ## Rebuild the gated SMP bins and regenerate mk/ci_smp_done.mk
	@echo "# GENERATED by 'make ci-smp-refresh' -- do not edit by hand." >  mk/ci_smp_done.mk.tmp
	@echo "# Each entry is <name>:<bin in bins/>:<exit PC>, read from the" >> mk/ci_smp_done.mk.tmp
	@echo "# linked ELF with nm so it cannot drift from the committed bin." >> mk/ci_smp_done.mk.tmp
	@echo "CI_SMP_TESTS := \\" >> mk/ci_smp_done.mk.tmp
	@for e in $(CI_SMP_BUILD); do \
	  tgt=$${e%%:*}; rest=$${e#*:}; args=$${rest%%:*}; rest=$${rest#*:}; \
	  src=$${rest%%:*}; staged=$${rest##*:}; \
	  echo "[ci-smp-refresh] $$tgt $$args -> $$staged"; \
	  rm -f $(BENCH_SRC)/crt.o $(BENCH_SRC)/syscalls.o; \
	  $(MAKE) $$tgt $$args >/dev/null || exit 1; \
	  cp $(BENCH_SRC)/$$src.bin $(BINS)/$$staged; \
	  pc=$$($(RISCV_BIN)/riscv64-unknown-elf-nm $(BENCH_SRC)/$$src.riscv \
	        | awk '$$3=="exit"{print $$1}'); \
	  if [ -z "$$pc" ]; then echo "::error::no exit symbol in $$src.riscv"; exit 1; fi; \
	  name=$$(basename $$staged -q4.bin); \
	  held=0; for h in $(CI_SMP_HOLD); do [ "$$h" = "$$name" ] && held=1; done; \
	  if [ $$held -eq 1 ]; then \
	    echo "[ci-smp-refresh]   (held out of the gate: $$name)"; \
	    printf "#   %s:%s:0x%x\n" "$$name" "$$staged" $$((0x80000000 + 0x$$pc)) \
	      >> mk/ci_smp_held.tmp; \
	    continue; \
	  fi; \
	  printf "  %s:%s:0x%x \\\\\n" "$$name" "$$staged" $$((0x80000000 + 0x$$pc)) \
	    >> mk/ci_smp_done.mk.tmp; \
	done
	@echo "" >> mk/ci_smp_done.mk.tmp
	@# Held-out entries go after the list terminates -- a comment inside it
	@# would be swallowed by the preceding line's backslash continuation.
	@if [ -s mk/ci_smp_held.tmp ]; then \
	  echo "# Built and committed, but deliberately NOT gating CI (CI_SMP_HOLD" >> mk/ci_smp_done.mk.tmp; \
	  echo "# in mk/ci_smp.mk carries the reason for each):" >> mk/ci_smp_done.mk.tmp; \
	  cat mk/ci_smp_held.tmp >> mk/ci_smp_done.mk.tmp; \
	fi
	@rm -f mk/ci_smp_held.tmp
	@mv mk/ci_smp_done.mk.tmp mk/ci_smp_done.mk
	@echo "[ci-smp-refresh] wrote mk/ci_smp_done.mk:"
	@cat mk/ci_smp_done.mk
	@echo "[ci-smp-refresh] now: git add -f bins/mt-*.bin mk/ci_smp_done.mk"
