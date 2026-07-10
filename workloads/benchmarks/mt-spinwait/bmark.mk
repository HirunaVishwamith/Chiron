#=======================================================================
# Makefile fragment for mt-spinwait
#-----------------------------------------------------------------------

mt_spinwait_c_src = \
	mt-spinwait.c \
	syscalls.c \

mt_spinwait_riscv_src = \
	crt.S \

mt_spinwait_c_objs     = $(patsubst %.c, %.o, $(mt_spinwait_c_src))
mt_spinwait_riscv_objs = $(patsubst %.S, %.o, $(mt_spinwait_riscv_src))

mt_spinwait_host_bin = mt-spinwait.host
$(mt_spinwait_host_bin) : $(mt_spinwait_c_src)
	$(HOST_COMP) $^ -o $(mt_spinwait_host_bin)

mt_spinwait_riscv_bin = mt-spinwait.riscv
$(mt_spinwait_riscv_bin) : $(mt_spinwait_c_objs) $(mt_spinwait_riscv_objs)
	$(RISCV_LINK) $(mt_spinwait_c_objs) $(mt_spinwait_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_spinwait_riscv_bin)

junk += $(mt_spinwait_c_objs) $(mt_spinwait_riscv_objs) \
        $(mt_spinwait_host_bin) $(mt_spinwait_riscv_bin)
