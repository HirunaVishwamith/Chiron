# fpga/Makefile — Kintex-7 (PCIe/XDMA) board bring-up for chiron.
#
#   make vivado    build the Vivado project + bitstream (needs fpga/hdl/fpgaTop.v,
#                  produced by `make fpga-verilog` at the repo root -- `make
#                  vivado` at the repo root does both in one step)
#   make program   JTAG-program the FPGA's configuration memory (NOT flash --
#                  flash-boot on this board has a history of getting stuck,
#                  so the flash path is intentionally not wired up at all)
#   make rescan    hot-remove/rescan the Xilinx PCIe device after programming
#
# Then from the repo root: `python fpga/run.py` to DMA-load the quad-core
# Linux image and get an interactive console.

CC = gcc
CFLAGS = -Wall -O2 -g

TARGET ?= kintex7
JOBS   ?= 24
DEBUG  ?= 0

# Where the setuid PCIe rescan helper lives on the host.
RESCAN_HELPER_BIN   ?= /usr/local/sbin/pcie_rescan_xilinx
RESCAN_HELPER_GROUP ?= github

.PHONY: vivado program rescan install_rescan_helper clean

vivado:   ## Build the Vivado project + bitstream (fpga/$(TARGET)/...)
	@if [ ! -f hdl/fpgaTop.v ]; then \
		echo "Error: hdl/fpgaTop.v not found -- run 'make fpga-verilog' at the repo root first"; \
		exit 1; \
	fi
	vivado -mode batch -nolog -nojournal -source build_kintex7.tcl -tclargs $(TARGET) $(DEBUG)

# Program FPGA configuration memory over JTAG (never flash).
# Usage: make program [BITFILE=<path_to_bit_file>]
BITFILE ?= $(TARGET)/$(TARGET).runs/impl_1/chiron_wrapper.bit
program:
	@if [ ! -f "$(BITFILE)" ]; then \
		echo "Error: Bit file not found: $(BITFILE)"; \
		echo "Run 'make vivado' first, or pass BITFILE=<path>."; \
		exit 1; \
	fi
	@bash -c 'set -o pipefail; vivado -mode batch -nolog -nojournal -source program_fpga.tcl -tclargs $(BITFILE) $(TARGET) 2>&1 | grep -v "^#"'
	sudo ./rescan_xilinx.sh

rescan:
	./rescan_xilinx.sh

pcie_rescan_xilinx: pcie_rescan_xilinx.c
	$(CC) $(CFLAGS) -o $@ $<

# One-time host install: build the setuid PCIe rescan helper and place
# it at /usr/local/sbin so unprivileged CI users (group $(RESCAN_HELPER_GROUP))
# can rescan Xilinx PCIe devices without sudo. Must be run as root.
install_rescan_helper: pcie_rescan_xilinx
	install -d -m 0755 $(dir $(RESCAN_HELPER_BIN))
	install -m 0755 pcie_rescan_xilinx $(RESCAN_HELPER_BIN)
	chown root:$(RESCAN_HELPER_GROUP) $(RESCAN_HELPER_BIN)
	chmod 4750 $(RESCAN_HELPER_BIN)
	ls -l $(RESCAN_HELPER_BIN)

clean:
	rm -f pcie_rescan_xilinx
	rm -f *.jou *.log
	rm -rf $(TARGET) hdl/fpgaTop.v board_xdc/kintex7_no_debug.xdc
