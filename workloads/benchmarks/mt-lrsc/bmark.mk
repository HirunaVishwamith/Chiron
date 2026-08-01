#=======================================================================
# Makefile fragment for mt-lrsc
#-----------------------------------------------------------------------

mt_lrsc_c_src = \
	mt-lrsc.c \
	syscalls.c \

mt_lrsc_riscv_src = \
	crt.S \

mt_lrsc_c_objs     = $(patsubst %.c, %.o, $(mt_lrsc_c_src))
mt_lrsc_riscv_objs = $(patsubst %.S, %.o, $(mt_lrsc_riscv_src))

mt_lrsc_host_bin = mt-lrsc.host
$(mt_lrsc_host_bin) : $(mt_lrsc_c_src)
	$(HOST_COMP) $^ -o $(mt_lrsc_host_bin)

mt_lrsc_riscv_bin = mt-lrsc.riscv
$(mt_lrsc_riscv_bin) : $(mt_lrsc_c_objs) $(mt_lrsc_riscv_objs)
	$(RISCV_LINK) $(mt_lrsc_c_objs) $(mt_lrsc_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_lrsc_riscv_bin)

junk += $(mt_lrsc_c_objs) $(mt_lrsc_riscv_objs) \
        $(mt_lrsc_host_bin) $(mt_lrsc_riscv_bin)
