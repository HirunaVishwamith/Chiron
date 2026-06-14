#=======================================================================
# UOM Multicore processor: Makefile for Mt-Tinyprograms
#-----------------------------------------------------------------------
# Hiruna Vishwamith
#
#-----------------------------------------------------------------------


# Define the RISC-V toolchain prefix
RISCV_PREFIX = riscv64-unknown-elf-
RISCV_GCC = $(RISCV_PREFIX)gcc
# RISCV_GCC_OPTS = -static -ffreestanding -fno-pic -O2 -fno-common -fno-builtin-printf -march=rv64ima_zicsr -mabi=lp64
# RISCV_GCC_OPTS = -mcmodel=medany -static -std=gnu99 -O2 -fno-common -fno-builtin-printf  -march=rv64ima_zicsr -mabi=lp64
RISCV_GCC_OPTS = -mcmodel=medany -static -std=gnu99 -O2 -fno-common -march=rv64ima_zicsr -mabi=lp64
RISCV_LINK_OPTS = -nostdlib -nostartfiles

# Include path for headers in ./common (if needed)
INCS = -I./common

# List of object files to be generated
OBJS = mt-fire.o syscalls.o crt.o

# List of source files
SRCS = mt-fire.c common/syscalls.c common/crt.S

# VPATH to help Make find source files in . and ./common
VPATH = .:./common

# Default target
all: mt-fire.riscv mt-fire.dump mt-fire.bin

# Individual dependencies.  This is the correct way to specify dependencies.
mt-fire.o: mt-fire.c

syscalls.o: common/syscalls.c

crt.o: common/crt.S

# Compile rule for .c files
%.o: %.c
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(INCS) -c $< -o $@

# Compile rule for .S files (Assembler)
%.o: %.S
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(INCS) -D__ASSEMBLY__=1 -c $< -o $@

# Link object files into the final executable
mt-fire.riscv: $(OBJS)
	$(RISCV_GCC) -T common/test.ld $(RISCV_LINK_OPTS) -o $@ $(OBJS)

# Generate disassembly dump
mt-fire.dump: mt-fire.riscv
	$(RISCV_PREFIX)objdump -D $< > $@

# Rule to convert ELF to binary
mt-fire.bin: mt-fire.riscv
	$(RISCV_PREFIX)objcopy -O binary $< $@

# Clean up generated files
clean:
	rm -f $(OBJS) mt-fire.riscv mt-fire.bin mt-fire.dump

# Declare phony targets
.PHONY: all clean