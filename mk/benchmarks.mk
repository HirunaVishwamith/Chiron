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

# completion: committed PC(s), optionally gated on a0 (x10)
vvadd_DONE  := --done-pc 0x800009a0 --done-pc 0x800009ac --done-a0 2
matmul_DONE := --done-pc 0x80000a04
filter_DONE := --done-pc 0x80000bc8
csaxpy_DONE := --done-pc 0x800009a4 --done-pc 0x80000998 --done-a0 0
histo_DONE  := --done-pc 0x80000a40

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
