#=======================================================================
# UCB CS250 Makefile fragment for benchmarks
#-----------------------------------------------------------------------
#
# Each benchmark directory should have its own fragment which
# essentially lists what the source files are and how to link them
# into an riscv and/or host executable. All variables should include
# the benchmark name as a prefix so that they are unique.
#

mt_histo_c_src = \
	mt_histo.c \
	syscalls.c \

mt_histo_riscv_src = \
	crt.S \

mt_histo_c_objs     = $(patsubst %.c, %.o, $(mt_histo_c_src))
mt_histo_riscv_objs = $(patsubst %.S, %.o, $(mt_histo_riscv_src))

mt_histo_host_bin = mt-histo.host
$(mt_histo_host_bin): $(mt_histo_c_src)
	$(HOST_COMP) $^ -o $(mt_histo_host_bin)

mt_histo_riscv_bin = mt-histo.riscv
$(mt_histo_riscv_bin): $(mt_histo_c_objs) $(mt_histo_riscv_objs)
	$(RISCV_LINK) $(mt_histo_c_objs) $(mt_histo_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_histo_riscv_bin) 

junk += $(mt_histo_c_objs) $(mt_histo_riscv_objs) \
        $(mt_histo_host_bin) $(mt_histo_riscv_bin)
