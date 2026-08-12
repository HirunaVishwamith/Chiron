# Bare-metal UART demos for Chiron (RV64IMA, no libm / no FPU).
#
#   make              — fire + cube + solid
#   make mt-cube.bin  — rotating wireframe cube
#   make mt-solid.bin — filled, shaded cube

RISCV_PREFIX    = riscv64-unknown-elf-
RISCV_GCC       = $(RISCV_PREFIX)gcc
RISCV_GCC_OPTS  = -mcmodel=medany -static -std=gnu99 -O2 -fno-common \
                  -fno-builtin-printf -march=rv64ima_zicsr -mabi=lp64 \
                  -DNUM_CORES=$(NUM_CORES)
RISCV_LINK_OPTS = -nostdlib -nostartfiles
NUM_CORES      ?= 4

INCS = -I./common -I./g3d
VPATH = .:./common:./g3d

COMMON_OBJS = syscalls.o crt.o

.PHONY: all clean host-smoke
all: mt-fire.bin mt-cube.bin mt-solid.bin

# Host-only projection check (not part of the RV64 image).
host-smoke: g3d/host_smoke.c g3d/g3d.c g3d/g3d.h
	gcc -O2 -I./g3d -I./common g3d/host_smoke.c g3d/g3d.c -o /tmp/g3d_smoke
	/tmp/g3d_smoke

%.o: %.c
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(INCS) -c $< -o $@

%.o: %.S
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(INCS) -D__ASSEMBLY__=1 -c $< -o $@

g3d.o: g3d/g3d.c g3d/g3d.h
	$(RISCV_GCC) $(RISCV_GCC_OPTS) $(INCS) -c $< -o $@

mt-fire.riscv: mt-fire.o $(COMMON_OBJS)
	$(RISCV_GCC) -T common/test.ld $(RISCV_LINK_OPTS) -o $@ $^

mt-cube.riscv: mt-cube.o g3d.o $(COMMON_OBJS)
	$(RISCV_GCC) -T common/test.ld $(RISCV_LINK_OPTS) -o $@ $^

mt-solid.riscv: mt-solid.o g3d.o $(COMMON_OBJS)
	$(RISCV_GCC) -T common/test.ld $(RISCV_LINK_OPTS) -o $@ $^

%.dump: %.riscv
	$(RISCV_PREFIX)objdump -D $< > $@

%.bin: %.riscv
	$(RISCV_PREFIX)objcopy -O binary $< $@

clean:
	rm -f *.o g3d/*.o mt-fire.riscv mt-fire.bin mt-fire.dump \
	      mt-cube.riscv mt-cube.bin mt-cube.dump \
	      mt-solid.riscv mt-solid.bin mt-solid.dump
