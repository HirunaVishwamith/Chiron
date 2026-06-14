#=======================================================================
# UCB CS250 Makefile fragment for benchmarks
#-----------------------------------------------------------------------
#
# Each benchmark directory should have its own fragment which
# essentially lists what the source files are and how to link them
# into an riscv and/or host executable. All variables should include
# the benchmark name as a prefix so that they are unique.
#

mt_csaxpy_c_src = \
	mt-csaxpy_main.c \
	mt-csaxpy.c \
	syscalls.c \

mt_csaxpy_riscv_src = \
	crt.S \

mt_csaxpy_c_objs     = $(patsubst %.c, %.o, $(mt_csaxpy_c_src))
mt_csaxpy_riscv_objs = $(patsubst %.S, %.o, $(mt_csaxpy_riscv_src))

mt_csaxpy_host_bin = mt-csaxpy.host
$(mt_csaxpy_host_bin): $(mt_csaxpy_c_src)
	$(HOST_COMP) $^ -o $(mt_csaxpy_host_bin)

mt_csaxpy_riscv_bin = mt-csaxpy.riscv
$(mt_csaxpy_riscv_bin): $(mt_csaxpy_c_objs) $(mt_csaxpy_riscv_objs)
	$(RISCV_LINK) $(mt_csaxpy_c_objs) $(mt_csaxpy_riscv_objs) -o $(mt_csaxpy_riscv_bin) $(RISCV_LINK_OPTS)

junk += $(mtcsaxpy_c_objs) $(mt_csaxpy_riscv_objs) \
        $(mt_csaxpy_host_bin) $(mt_csaxpy_riscv_bin)
