#=======================================================================
# Makefile fragment for mt-divburst
#-----------------------------------------------------------------------

mt_divburst_c_src = \
	mt-divburst.c \
	syscalls.c \

mt_divburst_riscv_src = \
	crt.S \

mt_divburst_c_objs     = $(patsubst %.c, %.o, $(mt_divburst_c_src))
mt_divburst_riscv_objs = $(patsubst %.S, %.o, $(mt_divburst_riscv_src))

mt_divburst_host_bin = mt-divburst.host
$(mt_divburst_host_bin) : $(mt_divburst_c_src)
	$(HOST_COMP) $^ -o $(mt_divburst_host_bin)

mt_divburst_riscv_bin = mt-divburst.riscv
$(mt_divburst_riscv_bin) : $(mt_divburst_c_objs) $(mt_divburst_riscv_objs)
	$(RISCV_LINK) $(mt_divburst_c_objs) $(mt_divburst_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_divburst_riscv_bin)

junk += $(mt_divburst_c_objs) $(mt_divburst_riscv_objs) \
        $(mt_divburst_host_bin) $(mt_divburst_riscv_bin)
