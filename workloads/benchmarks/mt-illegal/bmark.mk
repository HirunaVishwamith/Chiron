#=======================================================================
# Makefile fragment for mt-illegal
#-----------------------------------------------------------------------

mt_illegal_c_src = \
	mt-illegal.c \
	syscalls.c \

mt_illegal_riscv_src = \
	crt.S \

mt_illegal_c_objs     = $(patsubst %.c, %.o, $(mt_illegal_c_src))
mt_illegal_riscv_objs = $(patsubst %.S, %.o, $(mt_illegal_riscv_src))

mt_illegal_host_bin = mt-illegal.host
$(mt_illegal_host_bin) : $(mt_illegal_c_src)
	$(HOST_COMP) $^ -o $(mt_illegal_host_bin)

mt_illegal_riscv_bin = mt-illegal.riscv
$(mt_illegal_riscv_bin) : $(mt_illegal_c_objs) $(mt_illegal_riscv_objs)
	$(RISCV_LINK) $(mt_illegal_c_objs) $(mt_illegal_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_illegal_riscv_bin)

junk += $(mt_illegal_c_objs) $(mt_illegal_riscv_objs) \
        $(mt_illegal_host_bin) $(mt_illegal_riscv_bin)
