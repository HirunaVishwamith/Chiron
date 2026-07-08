# ── Scaled (s1-sN) benchmark binary generation ───────────────────────────────
# Ports the "active.h" scale-selection convention (from the standalone Quad/
# benchmark suite) into chiron's build: for each family/scale, stage the
# right sN.h as active.h, do a full clean rebuild (the plain %.o: %.c rule has
# no header dependency tracking, so a stale .o from a different scale would
# not otherwise get invalidated), then copy the result into bins/.
#
# crt.S now hardcodes NUM_CORES=4 (every hart always participates), so the
# single-core-labeled and quad-core-labeled builds differ only in which
# harness verifies them (lockstep vs profile_quad) and in bin naming --
# passing -DNUM_CORES=4 for the "-q4" family is kept for clarity even though
# it matches crt.S's own default.
#
# Usage:  make bins-scale       -> bins/<dir>-sN.bin      (lockstep-verified family)
#         make bins-scale-q4    -> bins/<dir>-sN-q4.bin   (profile_quad-verified family)
#         make bins-scale-all   -> both

# NUM_CORES defaults to 4 (defined in mk/bins_quad.mk); override on the command
# line, e.g.  make bins-scale NUM_CORES=1   or   make bins-scale-q4 NUM_CORES=8 .
# Both the single-core (bins-scale) and quad (bins-scale-q4) commands bake the
# chosen count into the benchmark via the compiler; crt.S #ifndef-guards its
# own default so this -D wins.
SCALE_GCC_OPTS := -mcmodel=medany -static -std=gnu99 -O2 -fno-common \
                   -fno-builtin-printf -fno-tree-loop-distribute-patterns \
                   -march=rv64ima_zicsr -mabi=lp64 -DNUM_CORES=$(NUM_CORES)
SCALE_GCC_OPTS_Q4 := $(SCALE_GCC_OPTS)

# name:dir:maxscale -- matmul has no s4/s5 dataset, capped at s3.
SCALE_FAMILIES := vvadd:mt-vvadd:5 matmul:mt-matmul:3 filter:mt-mask-sfilter:5 \
                  csaxpy:mt-csaxpy:5 histo:mt-histo:5

# `make clean` in $(BENCH_SRC)/Makefile only clears the default `bmarks`
# (mt-vvadd) unless bmarks= is passed to the clean invocation too -- and the
# plain %.o: %.c rule has no header dependency tracking, so a stale .o from
# the PREVIOUS scale iteration of the same benchmark would silently survive
# and never get relinked against the new active.h. Wipe build outputs
# directly instead of trusting `make clean`'s per-family junk list.
define SCALE_WIPE
	rm -f $(BENCH_SRC)/*.o $(BENCH_SRC)/*.riscv $(BENCH_SRC)/*.riscv.dump $(BENCH_SRC)/*.bin
endef

.PHONY: bins-scale bins-scale-q4 bins-scale-all
bins-scale:         ## Build bins/<dir>-sN.bin for all families/scales
	@mkdir -p $(BINS)
	@for fam in $(SCALE_FAMILIES); do \
	  rest=$${fam#*:}; dir=$${rest%%:*}; max=$${rest##*:}; \
	  for n in $$(seq 1 $$max); do \
	    cp "$(BENCH_SRC)/$$dir/s$$n.h" "$(BENCH_SRC)/$$dir/active.h"; \
	    $(SCALE_WIPE); \
	    $(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv bmarks="$$dir" RISCV_GCC_OPTS="$(SCALE_GCC_OPTS)"; \
	    cp "$(BENCH_SRC)/$$dir.bin" "$(BINS)/$$dir-s$$n.bin"; \
	    echo "[bins-scale] staged: $(BINS)/$$dir-s$$n.bin"; \
	  done; \
	done
	@$(SCALE_WIPE)

bins-scale-q4:      ## Build bins/<dir>-sN-q4.bin for all families/scales
	@mkdir -p $(BINS)
	@for fam in $(SCALE_FAMILIES); do \
	  rest=$${fam#*:}; dir=$${rest%%:*}; max=$${rest##*:}; \
	  for n in $$(seq 1 $$max); do \
	    cp "$(BENCH_SRC)/$$dir/s$$n.h" "$(BENCH_SRC)/$$dir/active.h"; \
	    $(SCALE_WIPE); \
	    $(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv bmarks="$$dir" RISCV_GCC_OPTS="$(SCALE_GCC_OPTS_Q4)"; \
	    cp "$(BENCH_SRC)/$$dir.bin" "$(BINS)/$$dir-s$$n-q4.bin"; \
	    echo "[bins-scale-q4] staged: $(BINS)/$$dir-s$$n-q4.bin"; \
	  done; \
	done
	@$(SCALE_WIPE)

bins-scale-all: bins-scale bins-scale-q4   ## Build both single-core and quad-core scaled bins
