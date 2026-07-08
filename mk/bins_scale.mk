# ── Scaled (s1-sN) benchmark binary generation ───────────────────────────────
# Ports the "active.h" scale-selection convention (from the standalone Quad/
# benchmark suite) into chiron's build: for each family/scale, stage the
# right sN.h as active.h, do a full clean rebuild (the plain %.o: %.c rule has
# no header dependency tracking, so a stale .o from a different scale would
# not otherwise get invalidated), then copy the result into bins/.
#
# crt.S bakes `li a1, NUM_CORES` (the hart count handed to thread_entry), so the
# core count MUST match the harness that runs the bin: the single-core family is
# verified by the single-core lockstep harness and therefore must be built with
# NUM_CORES=1 (otherwise core 0 computes only 1/Nth of the work and then spins
# forever on an N-way barrier -> register mismatch + timeout). The "-q4" family
# is verified by the 4-core profile_quad harness and is built with NUM_CORES=4.
# crt.S #ifndef-guards its own default, so the -D injected here always wins.
#
# Usage:  make bins-scale       -> bins/<dir>-sN.bin      (single-core, NUM_CORES=1)
#         make bins-scale-q4    -> bins/<dir>-sN-q4.bin   (quad-core,   NUM_CORES=4)
#         make bins-scale-all   -> both

# Per-family default core count. Override BOTH families explicitly from the
# command line with e.g.  make bins-scale NUM_CORES=2 .  When NUM_CORES is NOT
# given on the command line, the single family builds for 1 core and the quad
# family for 4 (the counts their respective harnesses actually run).
ifeq ($(origin NUM_CORES),command line)
  SCALE_NC_SINGLE := $(NUM_CORES)
  SCALE_NC_QUAD   := $(NUM_CORES)
else
  SCALE_NC_SINGLE := 1
  SCALE_NC_QUAD   := 4
endif

SCALE_GCC_OPTS_BASE := -mcmodel=medany -static -std=gnu99 -O2 -fno-common \
                   -fno-builtin-printf -fno-tree-loop-distribute-patterns \
                   -march=rv64ima_zicsr -mabi=lp64
SCALE_GCC_OPTS    := $(SCALE_GCC_OPTS_BASE) -DNUM_CORES=$(SCALE_NC_SINGLE)
SCALE_GCC_OPTS_Q4 := $(SCALE_GCC_OPTS_BASE) -DNUM_CORES=$(SCALE_NC_QUAD)

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
