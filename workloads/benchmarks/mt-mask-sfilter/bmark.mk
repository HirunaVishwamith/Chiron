#=======================================================================
# UCB CS250 Makefile fragment for benchmarks
#-----------------------------------------------------------------------
#
# Each benchmark directory should have its own fragment which
# essentially lists what the source files are and how to link them
# into an riscv and/or host executable. All variables should include
# the benchmark name as a prefix so that they are unique.
#

mt_mask_sfilter_c_src = \
	mt-mask-sfilter.c \
	mask-sfilter.c \
	syscalls.c \

mt_mask_sfilter_riscv_src = \
	crt.S \

mt_mask_sfilter_c_objs     = $(patsubst %.c, %.o, $(mt_mask_sfilter_c_src))
mt_mask_sfilter_riscv_objs = $(patsubst %.S, %.o, $(mt_mask_sfilter_riscv_src))

mt_mask_sfilter_host_bin = mt-mask-sfilter.host
$(mt_mask_sfilter_host_bin) : $(mt_mask_sfilter_c_src)
	$(HOST_COMP) $^ -o $(mt_mask_sfilter_host_bin)

mt_mask_sfilter_riscv_bin = mt-mask-sfilter.riscv
$(mt_mask_sfilter_riscv_bin) : $(mt_mask_sfilter_c_objs) $(mt_mask_sfilter_riscv_objs)
	$(RISCV_LINK) $(mt_mask_sfilter_c_objs) $(mt_mask_sfilter_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_mask_sfilter_riscv_bin)

junk += $(mt_mask_sfilter_c_objs) $(mt_mask_sfilter_riscv_objs) \
        $(mt_mask_sfilter_host_bin) $(mt_mask_sfilter_riscv_bin)
