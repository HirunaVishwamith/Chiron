# ── Benchmark manifest — the single source of truth ──────────────────────────
# Maps each short benchmark name to its .bin basename and its committed-PC
# completion spec (verified against the golden model). Both `lockstep` and
# `profile` read this, so completion PCs live in exactly one place.

BENCHES := vvadd matmul filter csaxpy histo

# short name -> .bin basename in $(BINS)
vvadd_base  := mt-vvadd
matmul_base := mt-matmul
filter_base := mt-mask-sfilter
csaxpy_base := mt-csaxpy
histo_base  := mt-histo
# q4-only diagnostic microbenchmark (see workloads/benchmarks/mt-seqlock/) --
# no single-core scale variants, built via `make seqlock-bin`.
seqlock_base := mt-seqlock
# q4-only SPLASH-3 Radix port (see workloads/benchmarks/mt-radix/) -- no
# single-core scale variants, built via `make radix-bin`.
radix_base := mt-radix
# q4-only Linux cpu_ops_spinwait secondary-wake protocol repro (see
# workloads/benchmarks/mt-spinwait/).
spinwait_base := mt-spinwait
# back-to-back M-extension divide regression (Linux __update_load_avg_se
# wedge repro, see workloads/benchmarks/mt-divburst/).
divburst_base := mt-divburst
# divide clusters under timer-IRQ fire + div-dependent branch shadows
# (see workloads/benchmarks/mt-divirq/).
divirq_base := mt-divirq
# q4-only SMP-coherency regressions (see mk/bins_quad.mk for what each covers).
# These are the repros for the RTL fixes on this branch; run them under
# profile_quad exactly like any other -q4 benchmark.
ipi_base     := mt-ipi
ipitmr_base  := mt-ipitmr
ipimux_base  := mt-ipimux
lrsc_base    := mt-lrsc
lrscirq_base := mt-lrscirq

# completion: committed PC(s), optionally gated on a0 (x10)
vvadd_DONE  := --done-pc 0x800009a0 --done-pc 0x800009ac --done-a0 2
matmul_DONE := --done-pc 0x80000a04
filter_DONE := --done-pc 0x80000bc8 --done-pc 0x80000bcc
csaxpy_DONE := --done-pc 0x800009a4 --done-pc 0x80000998 --done-a0 0
histo_DONE  := --done-pc 0x80000a40
seqlock_DONE := --done-pc 0x80000aa0
radix_DONE  := --done-pc 0x80000c68
spinwait_DONE := --done-pc 0x80000a9c
divburst_DONE := --done-pc 0x800009e4 --done-a0 0
divirq_DONE := --done-pc 0x80000b68 --done-a0 0
ipi_DONE     := --done-pc 0x80000bd4 --done-a0 0
ipitmr_DONE  := --done-pc 0x80000f54 --done-a0 0
ipimux_DONE  := --done-pc 0x80000dd4 --done-a0 0
lrsc_DONE    := --done-pc 0x80000cdc --done-a0 0
lrscirq_DONE := --done-pc 0x80000b54 --done-a0 0

# Resolve BENCH=<family>-s<scale> (default vvadd-s1) into a bin path + done spec.
# None of the family names contain "-s", so splitting on it is unambiguous.
BENCH ?= vvadd-s1
FAM   := $(firstword $(subst -s, ,$(BENCH)))
SCALE := $(lastword  $(subst -s, ,$(BENCH)))
BIN   := $(BINS)/$($(FAM)_base)-s$(SCALE).bin
DONE  := $($(FAM)_DONE)

# Quad-core benchmark families verified to complete on the RTL (used by make test).
# csaxpy-q4 uses DATA_SIZE=10000 (≡ s5 scale) which triggers the CCU deadlock.
REGRESSION_Q4 := vvadd
