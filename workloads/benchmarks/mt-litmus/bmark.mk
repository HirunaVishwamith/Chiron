#=======================================================================
# Makefile fragment for mt-litmus
#-----------------------------------------------------------------------

mt_litmus_c_src = \
	mt-litmus.c \
	syscalls.c \

mt_litmus_riscv_src = \
	crt.S \

mt_litmus_c_objs     = $(patsubst %.c, %.o, $(mt_litmus_c_src))
mt_litmus_riscv_objs = $(patsubst %.S, %.o, $(mt_litmus_riscv_src))

mt_litmus_host_bin = mt-litmus.host
$(mt_litmus_host_bin) : $(mt_litmus_c_src)
	$(HOST_COMP) $^ -o $(mt_litmus_host_bin)

mt_litmus_riscv_bin = mt-litmus.riscv
$(mt_litmus_riscv_bin) : $(mt_litmus_c_objs) $(mt_litmus_riscv_objs)
	$(RISCV_LINK) $(mt_litmus_c_objs) $(mt_litmus_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_litmus_riscv_bin)

junk += $(mt_litmus_c_objs) $(mt_litmus_riscv_objs) \
        $(mt_litmus_host_bin) $(mt_litmus_riscv_bin)
