#=======================================================================
# Makefile fragment for mt-seqlock
#-----------------------------------------------------------------------

mt_seqlock_c_src = \
	mt-seqlock.c \
	syscalls.c \

mt_seqlock_riscv_src = \
	crt.S \

mt_seqlock_c_objs     = $(patsubst %.c, %.o, $(mt_seqlock_c_src))
mt_seqlock_riscv_objs = $(patsubst %.S, %.o, $(mt_seqlock_riscv_src))

mt_seqlock_host_bin = mt-seqlock.host
$(mt_seqlock_host_bin) : $(mt_seqlock_c_src)
	$(HOST_COMP) $^ -o $(mt_seqlock_host_bin)

mt_seqlock_riscv_bin = mt-seqlock.riscv
$(mt_seqlock_riscv_bin) : $(mt_seqlock_c_objs) $(mt_seqlock_riscv_objs)
	$(RISCV_LINK) $(mt_seqlock_c_objs) $(mt_seqlock_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_seqlock_riscv_bin)

junk += $(mt_seqlock_c_objs) $(mt_seqlock_riscv_objs) \
        $(mt_seqlock_host_bin) $(mt_seqlock_riscv_bin)
