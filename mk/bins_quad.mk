# ── Quad-core (NUM_CORES=4) benchmark binary generation ──────────────────────
# Builds all 5 benchmarks with -DNUM_CORES=4 injected via RISCV_GCC_OPTS.
# The sub-Makefile doesn't support OBJDIR, so we build in-place then copy.
# A clean pass before and after avoids stale s/c objects cross-contaminating.
#
# Usage:  make bins-q4      → produces bins/mt-*-q4.bin for all 5 benchmarks

QUAD_BMARKS := mt-vvadd mt-matmul mt-mask-sfilter mt-histo mt-csaxpy

QUAD_GCC_OPTS := -mcmodel=medany -static -std=gnu99 -O2 -fno-common \
                 -fno-builtin-printf -march=rv64ima_zicsr -mabi=lp64 \
                 -DNUM_CORES=4

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
