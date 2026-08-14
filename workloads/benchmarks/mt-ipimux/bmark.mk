#=======================================================================
# Makefile fragment for mt-ipimux
#-----------------------------------------------------------------------

mt_ipimux_c_src = \
	mt-ipimux.c \
	syscalls.c \

mt_ipimux_riscv_src = \
	crt.S \

mt_ipimux_c_objs     = $(patsubst %.c, %.o, $(mt_ipimux_c_src))
mt_ipimux_riscv_objs = $(patsubst %.S, %.o, $(mt_ipimux_riscv_src))

mt_ipimux_host_bin = mt-ipimux.host
$(mt_ipimux_host_bin) : $(mt_ipimux_c_src)
	$(HOST_COMP) $^ -o $(mt_ipimux_host_bin)

mt_ipimux_riscv_bin = mt-ipimux.riscv
$(mt_ipimux_riscv_bin) : $(mt_ipimux_c_objs) $(mt_ipimux_riscv_objs)
	$(RISCV_LINK) $(mt_ipimux_c_objs) $(mt_ipimux_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_ipimux_riscv_bin)

junk += $(mt_ipimux_c_objs) $(mt_ipimux_riscv_objs) \
        $(mt_ipimux_host_bin) $(mt_ipimux_riscv_bin)
