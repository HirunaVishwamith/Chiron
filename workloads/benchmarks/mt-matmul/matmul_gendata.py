#!/usr/bin/env python3
"""
matmul_gendata.py

Simple script which creates an input data set and the reference data
for the matmul benchmark. Converted from Perl to Python3, saving output
in exact C-header format into a .txt file.
"""
import argparse
import random
import sys

def parse_args():
    parser = argparse.ArgumentParser(
        description='Generate random matrices, multiply, and dump as C arrays to a file')
    parser.add_argument('--size', type=int, default=1000,
                        help='size of input data [1000]')
    parser.add_argument('--seed', type=int, default=1,
                        help='random seed [1]')
    parser.add_argument('--output', type=str, default='dataset.txt',
                        help='output file name [dataset.txt]')
    return parser.parse_args()


def mmult(m1, m2):
    rows1, cols1 = len(m1), len(m1[0])
    rows2, cols2 = len(m2), len(m2[0])
    result = [[0]*cols2 for _ in range(rows1)]
    for i in range(rows1):
        for j in range(cols2):
            acc = 0
            for k in range(cols1):
                acc += m1[i][k] * m2[k][j]
            result[i][j] = acc
    return result


def write_array(name, flat, size, f):
    num_cols = 20
    total = len(flat)
    f.write(f"static data_t {name}[ARRAY_SIZE] = \n")
    f.write("{\n")
    if total <= num_cols:
        f.write("  ")
        line = ', '.join(f"{val:3d}" for val in flat)
        f.write(line + '\n')
    else:
        rows = total // num_cols
        for r in range(rows):
            f.write("  ")
            row_vals = flat[r*num_cols:(r+1)*num_cols]
            line = ', '.join(f"{v:3d}" for v in row_vals)
            f.write(line + ',\n')
        rem = total - rows*num_cols
        if rem:
            f.write("  ")
            tail = flat[rows*num_cols:]
            line = ', '.join(f"{v:3d}" for v in tail)
            f.write(line + '\n')
    f.write("};\n\n")


def main():
    args = parse_args()
    size = args.size
    random.seed(args.seed)

    # generate matrices
    mat1 = [[random.randrange(4) for _ in range(size)] for _ in range(size)]
    mat2 = [[random.randrange(4) for _ in range(size)] for _ in range(size)]

    # multiply
    mat_res = mmult(mat1, mat2)

    # flatten
    flat1 = [mat1[i][j] for i in range(size) for j in range(size)]
    flat2 = [mat2[i][j] for i in range(size) for j in range(size)]
    flats = [mat_res[i][j] for i in range(size) for j in range(size)]

    # write to file
    with open(args.output, 'w') as f:
        f.write("#ifndef __DATASET_H\n")
        f.write("#define __DATASET_H\n\n")
        f.write(f"#define ARRAY_SIZE {size*size}\n\n")
        f.write(f"#define DIM_SIZE {size}\n\n")
        f.write("typedef int data_t;\n\n")
        write_array("input1_data", flat1, size, f)
        write_array("input2_data", flat2, size, f)
        write_array("verify_data", flats, size, f)
        f.write("#endif //__DATASET_H\n")

if __name__ == '__main__':
    main()
 
