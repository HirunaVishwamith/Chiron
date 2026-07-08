#=======================================================================
# Makefile fragment for mt-radix
#-----------------------------------------------------------------------

mt_radix_c_src = \
	mt-radix.c \
	syscalls.c \

mt_radix_riscv_src = \
	crt.S \

mt_radix_c_objs     = $(patsubst %.c, %.o, $(mt_radix_c_src))
mt_radix_riscv_objs = $(patsubst %.S, %.o, $(mt_radix_riscv_src))

mt_radix_host_bin = mt-radix.host
$(mt_radix_host_bin) : $(mt_radix_c_src)
	$(HOST_COMP) $^ -o $(mt_radix_host_bin)

mt_radix_riscv_bin = mt-radix.riscv
$(mt_radix_riscv_bin) : $(mt_radix_c_objs) $(mt_radix_riscv_objs)
	$(RISCV_LINK) $(mt_radix_c_objs) $(mt_radix_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_radix_riscv_bin)

junk += $(mt_radix_c_objs) $(mt_radix_riscv_objs) \
        $(mt_radix_host_bin) $(mt_radix_riscv_bin)
