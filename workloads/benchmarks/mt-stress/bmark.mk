#=======================================================================
# Makefile fragment for mt-stress (GENERATED source — see tools/gen_stress.py)
#-----------------------------------------------------------------------

mt_stress_c_src = \
	mt-stress.c \
	syscalls.c \

mt_stress_riscv_src = \
	crt.S \

mt_stress_c_objs     = $(patsubst %.c, %.o, $(mt_stress_c_src))
mt_stress_riscv_objs = $(patsubst %.S, %.o, $(mt_stress_riscv_src))

mt_stress_host_bin = mt-stress.host
$(mt_stress_host_bin) : $(mt_stress_c_src)
	$(HOST_COMP) $^ -o $(mt_stress_host_bin)

mt_stress_riscv_bin = mt-stress.riscv
$(mt_stress_riscv_bin) : $(mt_stress_c_objs) $(mt_stress_riscv_objs)
	$(RISCV_LINK) $(mt_stress_c_objs) $(mt_stress_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_stress_riscv_bin)

junk += $(mt_stress_c_objs) $(mt_stress_riscv_objs) \
        $(mt_stress_host_bin) $(mt_stress_riscv_bin)
