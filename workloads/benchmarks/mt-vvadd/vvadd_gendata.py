#!/usr/bin/env python3
"""
vvadd_gendata.py

Simple script which creates an input data set and reference data
for the vvadd benchmark. Converted from Perl to Python3, saving output
in exact C-array format into a .txt file.
"""
import argparse
import random

def parse_args():
    parser = argparse.ArgumentParser(
        description='Generate random vectors, add, and dump as C arrays to a file')
    parser.add_argument('--size', type=int, default=1000,
                        help='size of input data [1000]')
    parser.add_argument('--seed', type=int, default=1,
                        help='random seed [1]')
    parser.add_argument('--output', type=str, default='vvadd_data.txt',
                        help='output file name [vvadd_data.txt]')
    return parser.parse_args()


def write_array(name, arr, f):
    num_cols = 20
    total = len(arr)
    f.write(f"static data_t {name}[DATA_SIZE] = \n")
    f.write("{\n")
    if total <= num_cols:
        f.write("  ")
        line = ', '.join(f"{val:3d}" for val in arr)
        f.write(line + '\n')
    else:
        rows = total // num_cols
        for r in range(rows):
            f.write("  ")
            row_vals = arr[r*num_cols:(r+1)*num_cols]
            line = ', '.join(f"{v:3d}" for v in row_vals)
            f.write(line + ',\n')
        rem = total - rows * num_cols
        if rem:
            f.write("  ")
            tail = arr[rows*num_cols:]
            line = ', '.join(f"{v:3d}" for v in tail)
            f.write(line + '\n')
    f.write("};\n\n")


def main():
    args = parse_args()
    size = args.size
    random.seed(args.seed)

    # generate vectors and sums
    v1 = [random.randrange(19) for _ in range(size)]
    v2 = [random.randrange(19) for _ in range(size)]
    vsum = [v1[i] + v2[i] for i in range(size)]

    # write to file
    with open(args.output, 'w') as f:
        f.write(f"#define DATA_SIZE {size}\n\n")
        f.write("#define data_t int\n\n")
        write_array('input_data_X', v1, f)
        write_array('input_data_Y', v2, f)
        write_array('verify_data_Z', vsum, f)

if __name__ == '__main__':
    main()
 
