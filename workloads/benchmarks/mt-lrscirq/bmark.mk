#=======================================================================
# Makefile fragment for mt-lrscirq
#-----------------------------------------------------------------------

mt_lrscirq_c_src = \
	mt-lrscirq.c \
	syscalls.c \

mt_lrscirq_riscv_src = \
	crt.S \

mt_lrscirq_c_objs     = $(patsubst %.c, %.o, $(mt_lrscirq_c_src))
mt_lrscirq_riscv_objs = $(patsubst %.S, %.o, $(mt_lrscirq_riscv_src))

mt_lrscirq_host_bin = mt-lrscirq.host
$(mt_lrscirq_host_bin) : $(mt_lrscirq_c_src)
	$(HOST_COMP) $^ -o $(mt_lrscirq_host_bin)

mt_lrscirq_riscv_bin = mt-lrscirq.riscv
$(mt_lrscirq_riscv_bin) : $(mt_lrscirq_c_objs) $(mt_lrscirq_riscv_objs)
	$(RISCV_LINK) $(mt_lrscirq_c_objs) $(mt_lrscirq_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_lrscirq_riscv_bin)

junk += $(mt_lrscirq_c_objs) $(mt_lrscirq_riscv_objs) \
        $(mt_lrscirq_host_bin) $(mt_lrscirq_riscv_bin)
