#=======================================================================
# Makefile fragment for mt-ipiwfi
#-----------------------------------------------------------------------

mt_ipiwfi_c_src = \
	mt-ipiwfi.c \
	syscalls.c \

mt_ipiwfi_riscv_src = \
	crt.S \

mt_ipiwfi_c_objs     = $(patsubst %.c, %.o, $(mt_ipiwfi_c_src))
mt_ipiwfi_riscv_objs = $(patsubst %.S, %.o, $(mt_ipiwfi_riscv_src))

mt_ipiwfi_host_bin = mt-ipiwfi.host
$(mt_ipiwfi_host_bin) : $(mt_ipiwfi_c_src)
	$(HOST_COMP) $^ -o $(mt_ipiwfi_host_bin)

mt_ipiwfi_riscv_bin = mt-ipiwfi.riscv
$(mt_ipiwfi_riscv_bin) : $(mt_ipiwfi_c_objs) $(mt_ipiwfi_riscv_objs)
	$(RISCV_LINK) $(mt_ipiwfi_c_objs) $(mt_ipiwfi_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_ipiwfi_riscv_bin)

junk += $(mt_ipiwfi_c_objs) $(mt_ipiwfi_riscv_objs) \
        $(mt_ipiwfi_host_bin) $(mt_ipiwfi_riscv_bin)
