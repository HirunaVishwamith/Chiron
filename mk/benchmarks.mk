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
# ── Completion PCs ───────────────────────────────────────────────────────────
# Each entry is the PC a benchmark parks on when it is finished (its exit stub),
# optionally gated on a0. THESE ARE ABSOLUTE ADDRESSES AND THEY MOVE: crt.o
# links first, so any change to workloads/benchmarks/common/crt.S shifts every
# one of them by the same amount, and a stale value means the harness never sees
# completion and burns its whole cycle budget instead of failing fast.
#
# `make done-pcs` prints the current exit address for every family so the table
# can be re-derived rather than guessed. (The extra-hart park in crt.S shifted
# everything by +0x08 from the 2026-08-15 stack-fix addresses.)
#
# s1 and s5 of the same family do not share one exit PC: DATA_SIZE changes
# immediates in verify/print and slides `exit`. List every scale's `j .`
# at exit. park_extra_hart at 0x80000148 is not an exit.
#
# Do NOT gate these on --done-a0. exit() is `while(1)` and does not keep
# the status argument in a0. The UART "error code: 0" line is the result
# check (`profile_quad` already fails on a nonzero error code). Result
# arrays are not printed unless the bin is built with -DDEBUG.
vvadd_DONE  := --done-pc 0x8000099c --done-pc 0x800009a4 --done-pc 0x800009ac
matmul_DONE := --done-pc 0x80000a00 --done-pc 0x80000a08
filter_DONE := --done-pc 0x80000bc4 --done-pc 0x80000bcc --done-pc 0x80000bc8 --done-pc 0x80000bd0
csaxpy_DONE := --done-pc 0x80000994 --done-pc 0x8000099c --done-pc 0x800009a4
histo_DONE  := --done-pc 0x80000a3c --done-pc 0x80000a44
seqlock_DONE := --done-pc 0x80000aac
radix_DONE  := --done-pc 0x80000c74
spinwait_DONE := --done-pc 0x80000aa8
divburst_DONE := --done-pc 0x800009f0 --done-a0 0
divirq_DONE := --done-pc 0x80000b74 --done-a0 0
ipi_DONE     := --done-pc 0x80000be0 --done-a0 0
ipitmr_DONE  := --done-pc 0x80000f60 --done-a0 0
ipimux_DONE  := --done-pc 0x80000de0 --done-a0 0
lrsc_DONE    := --done-pc 0x80000ce8 --done-a0 0
lrscirq_DONE := --done-pc 0x80000b60 --done-a0 0

# Resolve BENCH=<family>-s<scale> (default vvadd-s1) into a bin path + done spec.
# None of the family names contain "-s", so splitting on it is unambiguous.
BENCH ?= vvadd-s1
FAM   := $(firstword $(subst -s, ,$(BENCH)))
SCALE := $(lastword  $(subst -s, ,$(BENCH)))
BIN   := $(BINS)/$($(FAM)_base)-s$(SCALE).bin
DONE  := $($(FAM)_DONE)

# Quad-core families `make test-q4` / `make regress-q4` must finish with
# error code 0. The everyday gate is vvadd; the full set is REGRESSION_Q4_ALL.
REGRESSION_Q4 := vvadd
REGRESSION_Q4_ALL := vvadd matmul filter histo csaxpy

# Unscaled name (`bins/mt-vvadd.bin`, `bins/mt-vvadd-q4.bin`) means this scale.
# bins-scale / bins-scale-q4 / bench-bin / bins-q4 refresh that name from the
# matching -sN image as part of creating the bins — there is no extra step.
# ci-bench still uses the max-scale images (s5, except matmul-s1).
vvadd_DEFAULT_SCALE  := 1
matmul_DEFAULT_SCALE := 1
filter_DEFAULT_SCALE := 1
csaxpy_DEFAULT_SCALE := 5
histo_DEFAULT_SCALE  := 5
