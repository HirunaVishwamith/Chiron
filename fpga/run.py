#!/usr/bin/env python3
"""
fpga/run.py -- load the quad-core Linux image onto the Kintex-7 board over
PCIe/XDMA and run an interactive console.

Low-level transport (raw AXI-Lite register read/write over /dev/xdma0_user,
bulk DMA over /dev/xdma0_h2c_0) follows the same pattern as user_dma_core.py
from the Andromeda project -- that file's DMA/PCIe plumbing, none of its
tensor-accelerator API, which chiron has no use for.

Register map (fpgaTop_0/s_axi, see src/main/scala/testbench/hostBridge.scala),
mapped at 0x0200_0000 in xdma_0/M_AXI_LITE's address space:
    0x00 CTRL    [0]=RUN (0=cores held in reset, 1=released)       R/W
    0x04 STATUS  [0]=RUN mirror  [1]=TX not-empty  [2]=RX full     R
    0x08 TX_DATA [7:0]=next console byte, [8]=valid; read dequeues R
    0x0C RX_DATA [7:0]=byte to inject; write enqueues              W

DDR3 is mapped at 0x8000_0000 in both xdma_0/M_AXI and fpgaTop_0/m_axi
(see build_kintex7.tcl) -- the same RISC-V physical address chiron's cores
already fetch/load/store at, so the kernel image is DMA'd straight to
0x8000_0000 with no translation.

Usage:
    python3 fpga/run.py [--image bins/linux-q4.bin] [--device xdma0]
"""
import argparse
import os
import select
import struct
import sys
import threading
import time

S_AXI_BASE = 0x02000000
REG_CTRL    = S_AXI_BASE + 0x00
REG_STATUS  = S_AXI_BASE + 0x04
REG_TX_DATA = S_AXI_BASE + 0x08
REG_RX_DATA = S_AXI_BASE + 0x0C

DRAM_BASE = 0x80000000

STATUS_RUN      = 1 << 0
STATUS_TX_VALID = 1 << 1
STATUS_RX_FULL  = 1 << 2
TX_DATA_VALID   = 1 << 8


class Board:
    def __init__(self, device: str):
        self.dev_user = f"/dev/{device}_user"
        self.dev_h2c  = f"/dev/{device}_h2c_0"

    def read_reg32(self, addr: int) -> int:
        fd = os.open(self.dev_user, os.O_RDONLY)
        try:
            data = os.pread(fd, 4, addr)
            return struct.unpack("<I", data)[0]
        finally:
            os.close(fd)

    def write_reg32(self, addr: int, value: int):
        fd = os.open(self.dev_user, os.O_RDWR)
        try:
            os.pwrite(fd, struct.pack("<I", value & 0xFFFFFFFF), addr)
        finally:
            os.close(fd)

    def dma_load_image(self, image_path: str, dram_addr: int = DRAM_BASE):
        with open(image_path, "rb") as f:
            data = f.read()
        fd = os.open(self.dev_h2c, os.O_RDWR)
        try:
            os.lseek(fd, dram_addr, os.SEEK_SET)
            written = os.write(fd, data)
            if written != len(data):
                raise IOError(f"short DMA write: {written}/{len(data)} bytes")
        finally:
            os.close(fd)
        print(f"[run.py] loaded {len(data)} bytes -> 0x{dram_addr:08x}")


def console_reader(board: Board, stop: threading.Event):
    """Drain the TX FIFO and print bytes as they arrive, until `stop` is set."""
    buf = bytearray()
    while not stop.is_set():
        status = board.read_reg32(REG_STATUS)
        if not (status & STATUS_TX_VALID):
            time.sleep(0.005)
            continue
        tx = board.read_reg32(REG_TX_DATA)
        if tx & TX_DATA_VALID:
            buf.append(tx & 0xFF)
            # Flush on newlines so prompts without a trailing \n still show up
            # promptly via the length-based flush below.
            if tx & 0xFF == 0x0A or len(buf) >= 64:
                sys.stdout.buffer.write(bytes(buf))
                sys.stdout.flush()
                buf.clear()
        if buf and not (board.read_reg32(REG_STATUS) & STATUS_TX_VALID):
            sys.stdout.buffer.write(bytes(buf))
            sys.stdout.flush()
            buf.clear()


def console_writer(board: Board, stop: threading.Event):
    """Forward typed lines from stdin to the RX FIFO, one character at a time."""
    while not stop.is_set():
        ready, _, _ = select.select([sys.stdin], [], [], 0.2)
        if not ready:
            continue
        line = sys.stdin.readline()
        if line == "":
            stop.set()
            break
        for ch in line:
            while board.read_reg32(REG_STATUS) & STATUS_RX_FULL:
                time.sleep(0.001)
            board.write_reg32(REG_RX_DATA, ord(ch))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", default="bins/linux-q4.bin",
                     help="kernel image to DMA into DDR3 (default: bins/linux-q4.bin)")
    ap.add_argument("--device", default="xdma0",
                     help="XDMA device name, i.e. /dev/<device>_* (default: xdma0)")
    ap.add_argument("--no-load", action="store_true",
                     help="skip the DMA image load / RUN release, just attach the console "
                          "(use if the board is already running)")
    args = ap.parse_args()

    board = Board(args.device)

    if not args.no_load:
        board.write_reg32(REG_CTRL, 0)  # hold cores in reset while we load
        board.dma_load_image(args.image)
        board.write_reg32(REG_CTRL, 1)  # release -- cores start fetching at 0x8000_0000
        print("[run.py] RUN asserted, cores released")

    print("[run.py] console attached (Ctrl-D to quit) --------------------------")

    stop = threading.Event()
    reader = threading.Thread(target=console_reader, args=(board, stop), daemon=True)
    writer = threading.Thread(target=console_writer, args=(board, stop), daemon=True)
    reader.start()
    writer.start()
    try:
        while not stop.is_set():
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()


if __name__ == "__main__":
    main()
