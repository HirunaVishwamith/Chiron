#!/usr/bin/env python3
"""
decode_riscv_log.py

Reads run.log, decodes the low 32 bits of the 3rd column as a RISC‑V instruction,
inserts the disassembly into column 4, shifts original cols 4→5 and 5→6,
and writes to stdout or an output file.
"""

import sys
from capstone import Cs, CS_ARCH_RISCV, CS_MODE_RISCV64

def decode_line(line, md):
    cols = line.strip().split()
    if len(cols) < 5:
        # skip malformed lines
        return None

    pc, addr, raw_val, col4, col5 = cols[:5]

    # Take the lowest 8 hex digits of the 3rd column:
    inst32 = int(raw_val[-8:], 16)

    # Capstone wants little-endian bytes:
    inst_bytes = inst32.to_bytes(4, byteorder='little')

    # Disassemble; there should be exactly one instruction
    decoded = ""
    for ins in md.disasm(inst_bytes, 0):
        decoded = f"{ins.mnemonic} {ins.op_str}".strip()
        break

    # Build the new columns:
    new_cols = [pc, addr, raw_val, decoded]

    # If there were more than 5 columns, append the extras:
    if len(cols) > 5:
        new_cols.extend(cols[5:])

    return " ".join(new_cols)

def main(in_fname, out_fname=None):
    # Initialize a RISC‑V 64-bit disassembler
    md = Cs(CS_ARCH_RISCV, CS_MODE_RISCV64)
    md.detail = False

    infile = open(in_fname, 'r')
    outfile = open(out_fname, 'w') if out_fname else sys.stdout

    for line in infile:
        decoded_line = decode_line(line, md)
        if decoded_line:
            print(decoded_line, file=outfile)

    infile.close()
    if out_fname:
        outfile.close()

if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(
        description="Decode low 32 bits of 3rd column in a RISC‑V log file"
    )
    p.add_argument(
        "input", help="Path to run.log"
    )
    p.add_argument(
        "-o", "--output",
        help="Path to write decoded log (defaults to stdout)"
    )
    args = p.parse_args()

    main(args.input, args.output)
