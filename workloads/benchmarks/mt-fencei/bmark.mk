#=======================================================================
# Makefile fragment for mt-fencei
#-----------------------------------------------------------------------

mt_fencei_c_src = \
	mt-fencei.c \
	syscalls.c \

mt_fencei_riscv_src = \
	crt.S \

mt_fencei_c_objs     = $(patsubst %.c, %.o, $(mt_fencei_c_src))
mt_fencei_riscv_objs = $(patsubst %.S, %.o, $(mt_fencei_riscv_src))

mt_fencei_host_bin = mt-fencei.host
$(mt_fencei_host_bin) : $(mt_fencei_c_src)
	$(HOST_COMP) $^ -o $(mt_fencei_host_bin)

mt_fencei_riscv_bin = mt-fencei.riscv
$(mt_fencei_riscv_bin) : $(mt_fencei_c_objs) $(mt_fencei_riscv_objs)
	$(RISCV_LINK) $(mt_fencei_c_objs) $(mt_fencei_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_fencei_riscv_bin)

junk += $(mt_fencei_c_objs) $(mt_fencei_riscv_objs) \
        $(mt_fencei_host_bin) $(mt_fencei_riscv_bin)
