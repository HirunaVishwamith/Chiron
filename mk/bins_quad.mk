# ── Quad-core (NUM_CORES=4) benchmark binary generation ──────────────────────
# Builds all benchmarks with -DNUM_CORES=4 injected via RISCV_GCC_OPTS.
# The sub-Makefile doesn't support OBJDIR, so we build in-place then copy.
# A clean pass before and after avoids stale s/c objects cross-contaminating.
#
# Usage:  make bins-q4      → produces bins/mt-*-q4.bin for all listed benchmarks

QUAD_BMARKS := mt-vvadd mt-matmul mt-mask-sfilter mt-histo mt-csaxpy mt-seqlock mt-radix mt-spinwait mt-divburst mt-divirq \
               mt-ipi mt-ipitmr mt-ipimux mt-lrsc mt-lrscirq

# Number of harts baked into the -q4 (and scale-q4) benchmarks. Override on the
# command line, e.g.  make bins-q4 NUM_CORES=8 .  crt.S guards its own default
# with #ifndef NUM_CORES, so this -D is what actually selects the hart count.
NUM_CORES ?= 4

QUAD_GCC_OPTS := -mcmodel=medany -static -std=gnu99 -O2 -fno-common \
                 -fno-builtin-printf -fno-tree-loop-distribute-patterns \
                 -march=rv64ima_zicsr -mabi=lp64 -DNUM_CORES=$(NUM_CORES)
# -fno-tree-loop-distribute-patterns: this is a nostdlib/freestanding target
# (no libc) -- without it, GCC silently rewrites simple zeroing/copying
# loops (e.g. mt-radix's per-pass histogram reset) into calls to memset/
# memcpy, which don't exist here and fail at link time.

.PHONY: bins-q4 bins-all
bins-q4:    ## Build all benchmarks with NUM_CORES=4 → bins/mt-*-q4.bin
	@echo "[bins-q4] Building all benchmarks with NUM_CORES=4..."
	@$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) clean 2>/dev/null || true
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="$(QUAD_BMARKS)" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	@for bm in $(QUAD_BMARKS); do \
	    src="$(BENCH_SRC)/$${bm}.bin"; \
	    dst="$(BINS)/$${bm}-q4.bin"; \
	    if [ -f "$$src" ]; then \
	        cp "$$src" "$$dst"; \
	        echo "[bins-q4] staged: $$dst"; \
	    else \
	        echo "[bins-q4] WARNING: $$src not found, skipped"; \
	    fi; \
	done
	@$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) clean 2>/dev/null || true
	@echo "[bins-q4] Done."

bins-all: bench-bin bins-q4   ## Build both single-core and quad-core bins

.PHONY: seqlock-bin
seqlock-bin:    ## Build just bins/mt-seqlock-q4.bin (fast iteration, no clean)
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-seqlock" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-seqlock.bin $(BINS)/mt-seqlock-q4.bin
	@echo "[seqlock-bin] staged: $(BINS)/mt-seqlock-q4.bin"

.PHONY: divburst-bin
divburst-bin:    ## Build just bins/mt-divburst-q4.bin (fast iteration, no clean)
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-divburst" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-divburst.bin $(BINS)/mt-divburst-q4.bin
	@echo "[divburst-bin] staged: $(BINS)/mt-divburst-q4.bin"

.PHONY: spinwait-bin
spinwait-bin:    ## Build just bins/mt-spinwait-q4.bin (fast iteration, no clean)
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-spinwait" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-spinwait.bin $(BINS)/mt-spinwait-q4.bin
	@echo "[spinwait-bin] staged: $(BINS)/mt-spinwait-q4.bin"

.PHONY: radix-bin
radix-bin:    ## Build just bins/mt-radix-q4.bin (fast iteration, no clean)
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-radix" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-radix.bin $(BINS)/mt-radix-q4.bin
	@echo "[radix-bin] staged: $(BINS)/mt-radix-q4.bin"

# ── SMP-coherency regression microbenchmarks ─────────────────────────────────
# Each one is the minimal bare-metal repro of a bug found while bringing up
# quad-core Linux; they are the regressions for the RTL fixes on this branch:
#   mt-ipi      plain CLINT MSIP -> trap path
#   mt-ipitmr   MSIP under timer-IRQ pressure  (msip set/clear collision,
#               unguarded allocate-branchMask XOR, ACE responseBuffer clobber)
#   mt-ipimux   Linux 6.3 ipi_mux layered protocol, all harts sender+receiver
#   mt-lrsc     four-hart contended lr/sc (reservation forward progress)
#   mt-lrscirq  the same under IRQ fire      (stale CleanUnique upgrade)
# Usage: make ipi-bin / ipitmr-bin / ipimux-bin / lrsc-bin / lrscirq-bin
define smp_bmark_rule
.PHONY: $(1)-bin
$(1)-bin:    ## Build just bins/mt-$(1)-q4.bin (fast iteration, no clean)
	$$(TOOLPATH) $$(MAKE) -C $$(BENCH_SRC) riscv \
	    bmarks="mt-$(1)" \
	    RISCV_GCC_OPTS="$$(QUAD_GCC_OPTS)"
	@mkdir -p $$(BINS)
	cp $$(BENCH_SRC)/mt-$(1).bin $$(BINS)/mt-$(1)-q4.bin
	@echo "[$(1)-bin] staged: $$(BINS)/mt-$(1)-q4.bin"
endef
$(foreach b,ipi ipitmr ipimux lrsc lrscirq,$(eval $(call smp_bmark_rule,$(b))))

# ── Constrained-random speculation stress (tools/gen_stress.py) ───────────────
# mt-stress.c is GENERATED, not written: every one of the four reallocated-slot
# bugs lived in a corner the directed benchmarks never reach (a divide still in
# flight when a mispredict resolves, a second divide parked behind it, an MMIO
# load in a branch shadow, AMO/LR-SC racing a peer). This emits programs whose
# only purpose is to sit in those corners, with data-dependent branches the
# predictor cannot learn.
#
# The program is NOT self-checking on purpose. The oracle is external:
#   single core -> `make lockstep` compares every commit against the emulator,
#   quad core   -> `make ci-check` asserts the invariants every cycle.
# Everything derives from SEED, so a failing run replays exactly.
SEED   ?= 1
BLOCKS ?= 240

# The stress mix emits fence.i, which binutils only accepts when the arch
# string names Zifencei. The other benchmarks never assemble one by hand, which
# is why the shared QUAD_GCC_OPTS does not carry it.
STRESS_GCC_OPTS := $(subst _zicsr,_zicsr_zifencei,$(QUAD_GCC_OPTS))

.PHONY: stress-gen stress-bin
stress-gen:  ## Generate workloads/benchmarks/mt-stress/mt-stress.c from SEED
	@python3 tools/gen_stress.py --seed $(SEED) --blocks $(BLOCKS) \
	    --out workloads/benchmarks/mt-stress/mt-stress.c

# Regenerate then build, so `make stress-bin SEED=7` is one reproducible step.
stress-bin: stress-gen                                                    ## Generate + build bins/mt-stress-q4.bin for SEED
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-stress" \
	    RISCV_GCC_OPTS="$(STRESS_GCC_OPTS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-stress.bin $(BINS)/mt-stress-q4.bin
	@echo "[stress-bin] staged: $(BINS)/mt-stress-q4.bin (seed=$(SEED))"
