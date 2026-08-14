#=======================================================================
# Makefile fragment for mt-llist
#-----------------------------------------------------------------------

mt_llist_c_src = \
	mt-llist.c \
	syscalls.c \

mt_llist_riscv_src = \
	crt.S \

mt_llist_c_objs     = $(patsubst %.c, %.o, $(mt_llist_c_src))
mt_llist_riscv_objs = $(patsubst %.S, %.o, $(mt_llist_riscv_src))

mt_llist_host_bin = mt-llist.host
$(mt_llist_host_bin) : $(mt_llist_c_src)
	$(HOST_COMP) $^ -o $(mt_llist_host_bin)

mt_llist_riscv_bin = mt-llist.riscv
$(mt_llist_riscv_bin) : $(mt_llist_c_objs) $(mt_llist_riscv_objs)
	$(RISCV_LINK) $(mt_llist_c_objs) $(mt_llist_riscv_objs) $(RISCV_LINK_OPTS) -o $(mt_llist_riscv_bin)

junk += $(mt_llist_c_objs) $(mt_llist_riscv_objs) \
        $(mt_llist_host_bin) $(mt_llist_riscv_bin)
