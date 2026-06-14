#=======================================================================
# UCB VLSI FLOW: Makefile for riscv-bmarks
#-----------------------------------------------------------------------
# Yunsup Lee (yunsup@cs.berkeley.edu)
#

XLEN ?= 64

default: all

src_dir = .

instname = riscv-bmarks
# instbasedir = $(UCB_VLSI_HOME)/install
instbasedir = ./install

#--------------------------------------------------------------------
# Sources
#--------------------------------------------------------------------

# bmarks = \
# 	mt-matmul \
# 	mt-vvadd \
# 	mt-mask-sfilter \
# 	mt-histo \
# 	mt-csaxpy \

bmarks = mt-vvadd



# bmarks_host = \
# 	median \
# 	mt-vvadd \
# 	qsort \
# 	towers \
# 	vvadd \
# 	multiply \
# 	spmv \
# 	vec-vvadd \
# 	vec-cmplxmult \
# 	vec-matmul \

#--------------------------------------------------------------------
# Build rules
#--------------------------------------------------------------------

HOST_OPTS = -std=gnu99 -DPREALLOCATE=0 -DHOST_DEBUG=1
HOST_COMP = gcc $(HOST_OPTS)

RISCV_PREFIX ?= riscv$(XLEN)-unknown-elf-
RISCV_GCC ?= $(RISCV_PREFIX)gcc
# RISCV_GCC_OPTS ?= -mcmodel=medany -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf  -march=RV64IMAFDXhwacha
RISCV_GCC_OPTS ?= -mcmodel=medany -static -std=gnu99 -O2 -fno-common -fno-builtin-printf  -march=rv64ima_zicsr -mabi=lp64 
# RISCV_GCC_OPTS ?=  -static -O -ffreestanding -fno-pic -fno-common -fno-builtin-printf  -march=rv64ima_zicsr -mabi=lp64 // dot use this not working the barrier function
RISCV_LINK ?= $(RISCV_GCC) -n -T $(src_dir)/common/test.ld $(incs)
# RISCV_LINK_MT ?= $(RISCV_GCC) -T $(src_dir)/common/test-mt.ld
RISCV_LINK_OPTS ?= -nostdlib -nostartfiles  -Wl,-v
# RISCV_OBJDUMP ?= $(RISCV_PREFIX)objdump --disassemble-all --disassemble-zeroes --section=.text --section=.text.startup --section=.data
RISCV_OBJDUMP ?= $(RISCV_PREFIX)objdump -D 

VPATH += $(addprefix $(src_dir)/, $(bmarks))
VPATH += $(src_dir)/common

incs  +=  -I$(src_dir)/libamf/src -I$(src_dir)/common $(addprefix -I$(src_dir)/, $(bmarks))
objs  :=

include $(patsubst %, $(src_dir)/%/bmark.mk, $(bmarks))

#------------------------------------------------------------
# Build and run benchmarks on riscv simulator

# List of files for all benchmarks
bmarks_riscv_elf  = $(addsuffix .riscv,  $(bmarks))
bmarks_riscv_dump = $(addsuffix .riscv.dump, $(bmarks))
bmarks_riscv_bin  = $(addsuffix .bin,  $(bmarks))
bmarks_riscv_out  = $(addsuffix .riscv.out,  $(bmarks))

bmarks_defs   = -DPREALLOCATE=1 -DHOST_DEBUG=0
bmarks_cycles = 80000

$(bmarks_riscv_dump): %.riscv.dump: %.riscv
	$(RISCV_OBJDUMP) $< > $@

# Rule to convert ELF to binary
$(bmarks_riscv_bin): %.bin: %.riscv
	$(RISCV_PREFIX)objcopy -O binary $< $@

%.o: %.c
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(bmarks_defs) \
	             -c $(incs) $< -o $@

%.o: %.S
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(bmarks_defs) -D__ASSEMBLY__=1 \
	             -c $(incs) $< -o $@

ifdef HWACHA
riscv: $(bmarks_riscv_dump) $(bmarks_riscv_bin)
	make -C hwacha
else
riscv: $(bmarks_riscv_dump) $(bmarks_riscv_bin)
endif


junk += $(bmarks_riscv_elf) $(bmarks_riscv_dump) $(bmarks_riscv_bin) 


#------------------------------------------------------------
# Default

all: riscv

#------------------------------------------------------------
# Install

date_suffix = $(shell date +%Y-%m-%d_%H-%M)
install_dir = $(instbasedir)/$(instname)-$(date_suffix)
latest_install = $(shell ls -1 -d $(instbasedir)/$(instname)* | tail -n 1)

install:
	mkdir $(install_dir)
	cp -r $(bmarks_riscv_elf) $(bmarks_riscv_dump) $(install_dir)

install-link:
	rm -rf $(instbasedir)/$(instname)
	ln -s $(latest_install) $(instbasedir)/$(instname)

#------------------------------------------------------------
# Clean up

clean:
	rm -rf $(objs) $(junk)
