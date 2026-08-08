#=======================================================================
# Makefile fragment for mt-csdwait
#-----------------------------------------------------------------------

mt_csdwait_c_src = \
	mt-csdwait.c \
	syscalls.c \

mt_csdwait_riscv_src = \
	crt.S \

mt_csdwait_c_objs     = $(patsubst %.c, %.o, $(mt_csdwait_c_src))
mt_csdwait_riscv_objs = $(patsubst %.S, %.o, $(mt_csdwait_riscv_src))

mt_csdwait_host_bin = mt-csdwait.host
$(mt_csdwait_host_bin) : $(mt_csdwait_c_src)
	$(HOST_COMP) $^ -o $(mt_csdwait_host_bin)

mt_csdwait_riscv_bin = mt-csdwait.riscv
$(mt_csdwait_riscv_bin) : $(mt_csdwait_c_objs) $(mt_csdwait_riscv_objs)
	$(RISCV_LINK) $(mt_csdwait_c_objs) $(mt_csdwait_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_csdwait_riscv_bin)

junk += $(mt_csdwait_c_objs) $(mt_csdwait_riscv_objs) \
        $(mt_csdwait_host_bin) $(mt_csdwait_riscv_bin)
