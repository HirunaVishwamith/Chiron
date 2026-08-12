# ── Workload .bin generation + staging ───────────────────────────────────────
# Build bare-metal demos and benchmark ELFs from source, objcopy them to flat
# .bin images, and stage them into $(BINS)/ where every harness loads them by
# path. The pre-scaled s1–s5 benchmark bins already live in $(BINS) and are left
# untouched (scale generation via gendata is a separate, manual step).

.PHONY: bins fire-bin cube-bin solid-bin bench-bin
bins: fire-bin cube-bin solid-bin bench-bin   ## Build + stage all workload .bin images into bins/

# Doom-fire bare-metal demo. (test.ld base / NUM_CORES / DATA_BASE / UART are
# already adapted for this single-core target.)
fire-bin:                  ## Build + stage the fire demo
	$(TOOLPATH) $(MAKE) -C $(DEMO_SRC) mt-fire.bin
	cp $(DEMO_SRC)/mt-fire.bin $(BINS)/mt-fire.bin

cube-bin:                  ## Build + stage the wireframe cube demo
	$(TOOLPATH) $(MAKE) -C $(DEMO_SRC) mt-cube.bin
	cp $(DEMO_SRC)/mt-cube.bin $(BINS)/mt-cube.bin

solid-bin:                 ## Build + stage the filled-cube demo
	$(TOOLPATH) $(MAKE) -C $(DEMO_SRC) mt-solid.bin
	cp $(DEMO_SRC)/mt-solid.bin $(BINS)/mt-solid.bin

# Base (unscaled) benchmark bins — builds whatever `bmarks` the sub-Makefile sets.
bench-bin:                 ## Build + stage base benchmark bins
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv
	@for f in $(BENCH_SRC)/*.bin; do cp $$f $(BINS)/; done

# File rule so `make fire` rebuilds the demo only when its sources change.
$(BINS)/mt-fire.bin: $(DEMO_SRC)/mt-fire.c $(DEMO_SRC)/common/crt.S $(DEMO_SRC)/common/test.ld
	$(MAKE) fire-bin

$(BINS)/mt-cube.bin: $(DEMO_SRC)/mt-cube.c $(DEMO_SRC)/g3d/g3d.c $(DEMO_SRC)/g3d/g3d.h \
                     $(DEMO_SRC)/common/crt.S $(DEMO_SRC)/common/test.ld
	$(MAKE) cube-bin

$(BINS)/mt-solid.bin: $(DEMO_SRC)/mt-solid.c $(DEMO_SRC)/g3d/g3d.c $(DEMO_SRC)/g3d/g3d.h \
                      $(DEMO_SRC)/common/crt.S $(DEMO_SRC)/common/test.ld
	$(MAKE) solid-bin
