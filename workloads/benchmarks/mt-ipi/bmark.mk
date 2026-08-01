#=======================================================================
# Makefile fragment for mt-ipi
#-----------------------------------------------------------------------

mt_ipi_c_src = \
	mt-ipi.c \
	syscalls.c \

mt_ipi_riscv_src = \
	crt.S \

mt_ipi_c_objs     = $(patsubst %.c, %.o, $(mt_ipi_c_src))
mt_ipi_riscv_objs = $(patsubst %.S, %.o, $(mt_ipi_riscv_src))

mt_ipi_host_bin = mt-ipi.host
$(mt_ipi_host_bin) : $(mt_ipi_c_src)
	$(HOST_COMP) $^ -o $(mt_ipi_host_bin)

mt_ipi_riscv_bin = mt-ipi.riscv
$(mt_ipi_riscv_bin) : $(mt_ipi_c_objs) $(mt_ipi_riscv_objs)
	$(RISCV_LINK) $(mt_ipi_c_objs) $(mt_ipi_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_ipi_riscv_bin)

junk += $(mt_ipi_c_objs) $(mt_ipi_riscv_objs) \
        $(mt_ipi_host_bin) $(mt_ipi_riscv_bin)
