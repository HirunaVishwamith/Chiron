#=======================================================================
# Makefile fragment for mt-divirq
#-----------------------------------------------------------------------

mt_divirq_c_src = \
	mt-divirq.c \
	syscalls.c \

mt_divirq_riscv_src = \
	crt.S \

mt_divirq_c_objs     = $(patsubst %.c, %.o, $(mt_divirq_c_src))
mt_divirq_riscv_objs = $(patsubst %.S, %.o, $(mt_divirq_riscv_src))

mt_divirq_host_bin = mt-divirq.host
$(mt_divirq_host_bin) : $(mt_divirq_c_src)
	$(HOST_COMP) $^ -o $(mt_divirq_host_bin)

mt_divirq_riscv_bin = mt-divirq.riscv
$(mt_divirq_riscv_bin) : $(mt_divirq_c_objs) $(mt_divirq_riscv_objs)
	$(RISCV_LINK) $(mt_divirq_c_objs) $(mt_divirq_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_divirq_riscv_bin)

junk += $(mt_divirq_c_objs) $(mt_divirq_riscv_objs) \
        $(mt_divirq_host_bin) $(mt_divirq_riscv_bin)
