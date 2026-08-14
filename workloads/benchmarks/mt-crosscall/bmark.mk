#=======================================================================
# Makefile fragment for mt-crosscall
#-----------------------------------------------------------------------

mt_crosscall_c_src = \
	mt-crosscall.c \
	syscalls.c \

mt_crosscall_riscv_src = \
	crt.S \

mt_crosscall_c_objs     = $(patsubst %.c, %.o, $(mt_crosscall_c_src))
mt_crosscall_riscv_objs = $(patsubst %.S, %.o, $(mt_crosscall_riscv_src))

mt_crosscall_host_bin = mt-crosscall.host
$(mt_crosscall_host_bin) : $(mt_crosscall_c_src)
	$(HOST_COMP) $^ -o $(mt_crosscall_host_bin)

mt_crosscall_riscv_bin = mt-crosscall.riscv
$(mt_crosscall_riscv_bin) : $(mt_crosscall_c_objs) $(mt_crosscall_riscv_objs)
	$(RISCV_LINK) $(mt_crosscall_c_objs) $(mt_crosscall_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_crosscall_riscv_bin)

junk += $(mt_crosscall_c_objs) $(mt_crosscall_riscv_objs) \
        $(mt_crosscall_host_bin) $(mt_crosscall_riscv_bin)
