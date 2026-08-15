# ── Quad-core (NUM_CORES=4) benchmark binary generation ──────────────────────
# Builds all benchmarks with -DNUM_CORES=4 injected via RISCV_GCC_OPTS.
# The sub-Makefile doesn't support OBJDIR, so we build in-place then copy.
# A clean pass before and after avoids stale s/c objects cross-contaminating.
#
# Usage:  make bins-q4      → produces bins/mt-*-q4.bin for all listed benchmarks

# The five scale families are built by bins-scale-q4 (every s1–s5 + the
# unscaled default-scale name). bins-q4 runs that, then these diagnostics.
QUAD_SCALE_BMARKS := mt-vvadd mt-matmul mt-mask-sfilter mt-histo mt-csaxpy
QUAD_MICRO_BMARKS := mt-seqlock mt-radix mt-spinwait mt-divburst mt-divirq \
                     mt-ipi mt-ipitmr mt-ipimux mt-lrsc mt-lrscirq
QUAD_BMARKS := $(QUAD_SCALE_BMARKS) $(QUAD_MICRO_BMARKS)

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
bins-q4: bins-scale-q4   ## All quad-core s1–s5 benches + diagnostic micros → bins/mt-*-q4.bin
	@echo "[bins-q4] diagnostic micros with NUM_CORES=$(NUM_CORES)..."
	@$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) clean 2>/dev/null || true
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="$(QUAD_MICRO_BMARKS)" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS)"
	@mkdir -p $(BINS)
	@for bm in $(QUAD_MICRO_BMARKS); do \
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

bins-all: bins bins-q4   ## Demos + all single-core and quad-core s1–s5 benches + micros

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
#   mt-illegal  executes a literal 0x00000000 word and requires that it TRAPS
#               (mcause=2, mtval=0) instead of jamming the ROB head forever --
#               the regression for the illegal-instruction trap. NOTE: a
#               pre-fix RTL does not fail this, it HANGS, so a harness timeout
#               is the expected regression signature.
$(foreach b,ipi ipitmr ipimux lrsc lrscirq illegal,$(eval $(call smp_bmark_rule,$(b))))

# ── mt-llist: cmpxchg (lr/sc) racing amoswap on one word ─────────────────────
# The Linux cross-call queue publishes with cmpxchg and drains with xchg, both
# on call_single_queue's head. Nothing else in the suite mixes the two atomics
# on a single address, and the /init hang leaves an entry locked but on no
# queue -- the signature of a lost push. PUSH_CAS=0 builds the same-atomic
# control. Like fencei-bin, the objects are removed first because only a -D
# changes between the two builds.
PUSH_CAS ?= 1
.PHONY: llist-bin crosscall-bin

# ── mt-crosscall: the whole cross-call protocol at once ──────────────────────
# csd_lock -> llist_add (cmpxchg) -> IPI -> drain with amoswap IN THE ISR ->
# fence.i -> csd_unlock, with the sender spinning in csd_lock_wait. The ISR
# drain is the ingredient mt-csdwait/mt-fencei/mt-llist/mt-ipi* each lack.
# Emits fence.i by hand, hence the Zifencei arch string.
CC_FENCEI ?= 1
crosscall-bin:  ## Build bins/mt-crosscall-q4.bin (CC_FENCEI=0 drops the fence.i)
	@rm -f $(BENCH_SRC)/mt-crosscall.o $(BENCH_SRC)/mt-crosscall.riscv \
	       $(BENCH_SRC)/mt-crosscall.bin
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-crosscall" \
	    RISCV_GCC_OPTS="$(STRESS_GCC_OPTS) -DCC_FENCEI=$(CC_FENCEI)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-crosscall.bin $(BINS)/mt-crosscall-q4.bin
	@echo "[crosscall-bin] staged: $(BINS)/mt-crosscall-q4.bin (CC_FENCEI=$(CC_FENCEI))"
llist-bin:  ## Build bins/mt-llist-q4.bin (PUSH_CAS=0 builds the control)
	@rm -f $(BENCH_SRC)/mt-llist.o $(BENCH_SRC)/mt-llist.riscv \
	       $(BENCH_SRC)/mt-llist.bin
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-llist" \
	    RISCV_GCC_OPTS="$(QUAD_GCC_OPTS) -DPUSH_CAS=$(PUSH_CAS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-llist.bin $(BINS)/mt-llist-q4.bin
	@echo "[llist-bin] staged: $(BINS)/mt-llist-q4.bin (PUSH_CAS=$(PUSH_CAS))"

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

# ── mt-fencei: is a release store issued just after fence.i still visible? ────
# The Linux /init hang leaves a csd locked with every call_single_queue empty
# and func = ipi_remote_fence_i, i.e. a target popped the entry, ran `fence.i`,
# and then csd_unlock()'s release store vanished. fence.i arms the D-cache
# clean-on-fence walker, so this builds that exact sequence. Emits a fence.i by
# hand, hence the Zifencei arch string it shares with mt-stress.
#   make fencei-bin            fence.i present (the suspect)
#   make fencei-bin FENCEI=0   identical program without it (the control)
FENCEI      ?= 1
DIRTY_LINES ?= 64
.PHONY: fencei-bin
fencei-bin:  ## Build bins/mt-fencei-q4.bin (FENCEI=0 builds the control)
	@# FENCEI/DIRTY_LINES only change -D flags, not the .c, so make would leave
	@# the stale object in place and silently hand back the previous image.
	@rm -f $(BENCH_SRC)/mt-fencei.o $(BENCH_SRC)/mt-fencei.riscv \
	       $(BENCH_SRC)/mt-fencei.bin
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-fencei" \
	    RISCV_GCC_OPTS="$(STRESS_GCC_OPTS) -DFENCEI=$(FENCEI) -DDIRTY_LINES=$(DIRTY_LINES)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-fencei.bin $(BINS)/mt-fencei-q4.bin
	@echo "[fencei-bin] staged: $(BINS)/mt-fencei-q4.bin (FENCEI=$(FENCEI) DIRTY_LINES=$(DIRTY_LINES))"

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

# ── mt-icoh: is freshly-written CODE visible to another hart's I-FETCH? ──────
# mt-fencei covers fence.i + a DATA release store and passes; nothing covered
# INSTRUCTION visibility, which is a different path (I-cache invalidate + refill
# vs D-cache walker writeback). That is the gap behind the Linux freeze where
# hart0 fetched 16 words of zeros at 0x81b03840. Needs zifencei for the hand
# written fence.i, and the objects are removed first because only a -D changes
# between A/B builds.
ICOH_FENCEI ?= 1
ICOH_ROUNDS ?= 64
# ICOH_SELF=1 makes the writer execute its OWN freshly written code (same-hart
# store -> fence.i -> fetch). That is the Linux signal-trampoline path; the
# cross-hart default passes, so the two are built and run as separate cases.
ICOH_SELF ?= 0
.PHONY: icoh-bin
icoh-bin:  ## Build bins/mt-icoh-q4.bin (ICOH_FENCEI=0 drops the writer's fence.i; ICOH_SELF=1 same-hart)
	@rm -f $(BENCH_SRC)/mt-icoh.o $(BENCH_SRC)/mt-icoh.riscv $(BENCH_SRC)/mt-icoh.bin
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-icoh" \
	    RISCV_GCC_OPTS="$(STRESS_GCC_OPTS) -DWRITER_FENCEI=$(ICOH_FENCEI) -DROUNDS=$(ICOH_ROUNDS) -DSELF_EXEC=$(ICOH_SELF)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-icoh.bin $(BINS)/mt-icoh-q4.bin
	@echo "[icoh-bin] staged: $(BINS)/mt-icoh-q4.bin (WRITER_FENCEI=$(ICOH_FENCEI) SELF_EXEC=$(ICOH_SELF))"

# ── mt-uartrx: does the console RX path actually deliver a keystroke? ────────
# The Linux boot parks at "buildroot login: " and needs `root`/`nproc` typed in.
# That depends on brand-new logic (uartlite RX in quard_uart.scala, the
# hostInput top-level port, linux_sim.cpp's stdin forwarding), and the natural
# place to discover a mistake in it is 14 h into a boot with no way to type.
# This exercises the same registers the Linux uartlite driver uses, in seconds.
# RX_CHARS must match the harness's RX_TEXT length.
RX_CHARS ?= 11
.PHONY: uartrx-bin
uartrx-bin:  ## Build bins/mt-uartrx-q4.bin (console RX round-trip test)
	@rm -f $(BENCH_SRC)/mt-uartrx.o $(BENCH_SRC)/mt-uartrx.riscv $(BENCH_SRC)/mt-uartrx.bin
	$(TOOLPATH) $(MAKE) -C $(BENCH_SRC) riscv \
	    bmarks="mt-uartrx" \
	    RISCV_GCC_OPTS="$(STRESS_GCC_OPTS) -DRX_CHARS=$(RX_CHARS)"
	@mkdir -p $(BINS)
	cp $(BENCH_SRC)/mt-uartrx.bin $(BINS)/mt-uartrx-q4.bin
	@echo "[uartrx-bin] staged: $(BINS)/mt-uartrx-q4.bin (RX_CHARS=$(RX_CHARS))"
