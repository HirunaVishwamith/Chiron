# ── Quad-core (NUM_CORES=4) benchmark binary generation ──────────────────────
# Builds all benchmarks with -DNUM_CORES=4 injected via RISCV_GCC_OPTS.
# The sub-Makefile doesn't support OBJDIR, so we build in-place then copy.
# A clean pass before and after avoids stale s/c objects cross-contaminating.
#
# Usage:  make bins-q4      → produces bins/mt-*-q4.bin for all listed benchmarks

QUAD_BMARKS := mt-vvadd mt-matmul mt-mask-sfilter mt-histo mt-csaxpy mt-seqlock mt-radix

QUAD_GCC_OPTS := -mcmodel=medany -static -std=gnu99 -O2 -fno-common \
                 -fno-builtin-printf -fno-tree-loop-distribute-patterns \
                 -march=rv64ima_zicsr -mabi=lp64 -DNUM_CORES=4
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

.PHONY: radix-bin
radix-bin:    ## Build just bins/mt-radix-q4.bin (fast iteration, no clean)
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-radix" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-radix.bin $(BINS)/mt-radix-q4.bin
	@echo "[radix-bin] staged: $(BINS)/mt-radix-q4.bin"
