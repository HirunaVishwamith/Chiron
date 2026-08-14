#=======================================================================
# Makefile fragment for mt-icoh
#-----------------------------------------------------------------------

mt_icoh_c_src = \
	mt-icoh.c \
	syscalls.c \

mt_icoh_riscv_src = \
	crt.S \

mt_icoh_c_objs     = $(patsubst %.c, %.o, $(mt_icoh_c_src))
mt_icoh_riscv_objs = $(patsubst %.S, %.o, $(mt_icoh_riscv_src))

mt_icoh_host_bin = mt-icoh.host
$(mt_icoh_host_bin) : $(mt_icoh_c_src)
	$(HOST_COMP) $^ -o $(mt_icoh_host_bin)

mt_icoh_riscv_bin = mt-icoh.riscv
$(mt_icoh_riscv_bin) : $(mt_icoh_c_objs) $(mt_icoh_riscv_objs)
	$(RISCV_LINK) $(mt_icoh_c_objs) $(mt_icoh_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_icoh_riscv_bin)

junk += $(mt_icoh_c_objs) $(mt_icoh_riscv_objs) \
        $(mt_icoh_host_bin) $(mt_icoh_riscv_bin)
