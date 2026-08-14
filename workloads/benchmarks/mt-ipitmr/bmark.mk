#=======================================================================
# Makefile fragment for mt-ipitmr
#-----------------------------------------------------------------------

mt_ipitmr_c_src = \
	mt-ipitmr.c \
	syscalls.c \

mt_ipitmr_riscv_src = \
	crt.S \

mt_ipitmr_c_objs     = $(patsubst %.c, %.o, $(mt_ipitmr_c_src))
mt_ipitmr_riscv_objs = $(patsubst %.S, %.o, $(mt_ipitmr_riscv_src))

mt_ipitmr_host_bin = mt-ipitmr.host
$(mt_ipitmr_host_bin) : $(mt_ipitmr_c_src)
	$(HOST_COMP) $^ -o $(mt_ipitmr_host_bin)

mt_ipitmr_riscv_bin = mt-ipitmr.riscv
$(mt_ipitmr_riscv_bin) : $(mt_ipitmr_c_objs) $(mt_ipitmr_riscv_objs)
	$(RISCV_LINK) $(mt_ipitmr_c_objs) $(mt_ipitmr_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_ipitmr_riscv_bin)

junk += $(mt_ipitmr_c_objs) $(mt_ipitmr_riscv_objs) \
        $(mt_ipitmr_host_bin) $(mt_ipitmr_riscv_bin)
