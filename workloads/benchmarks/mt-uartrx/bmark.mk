#=======================================================================
# Makefile fragment for mt-uartrx
#-----------------------------------------------------------------------

mt_uartrx_c_src = \
	mt-uartrx.c \
	syscalls.c \

mt_uartrx_riscv_src = \
	crt.S \

mt_uartrx_c_objs     = $(patsubst %.c, %.o, $(mt_uartrx_c_src))
mt_uartrx_riscv_objs = $(patsubst %.S, %.o, $(mt_uartrx_riscv_src))

mt_uartrx_host_bin = mt-uartrx.host
$(mt_uartrx_host_bin) : $(mt_uartrx_c_src)
	$(HOST_COMP) $^ -o $(mt_uartrx_host_bin)

mt_uartrx_riscv_bin = mt-uartrx.riscv
$(mt_uartrx_riscv_bin) : $(mt_uartrx_c_objs) $(mt_uartrx_riscv_objs)
	$(RISCV_LINK) $(mt_uartrx_c_objs) $(mt_uartrx_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_uartrx_riscv_bin)

junk += $(mt_uartrx_c_objs) $(mt_uartrx_riscv_objs) \
        $(mt_uartrx_host_bin) $(mt_uartrx_riscv_bin)
