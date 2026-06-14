//**************************************************************************
// mask-sfilter.c
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 10/03/2025
//
//--------------------------------------------------------------------------


#include <stdio.h>
#define data_t int

// mask_sfilter function
// called on each core
void __attribute__((noinline)) mask_sfilter(int coreid, int ncores, 
        const data_t* input, data_t* output, short* mask, int input_sz,
        data_t m0, data_t m1, data_t m2, data_t m3, data_t m4, data_t m5, 
        data_t m6, data_t m7, data_t m8) {
    //input_sz is one dimension of square matrix
    //assume that input_sz is divisible by # cores for now
    const int rows = input_sz / ncores;
    int ind;
    int i0, i1, i2, i3, i4, i5, i6, i7, i8;

    for (int j = coreid*rows; j < (coreid+1)*rows; j++) {
        if (j == input_sz-1 || j == 0) {
            // top/bottom row
            continue;
        }
        // skip leftmost/rightmost column
        for (int i = 1; i < input_sz-1; i++) {
            ind = i+j*input_sz;
            if (mask[ind]) {
                i0 = input[(i-1)+(j-1)*input_sz]*m0;
                i1 = input[(i)  +(j-1)*input_sz]*m1;
                i2 = input[(i+1)+(j-1)*input_sz]*m2;
                i3 = input[(i-1)+(j)  *input_sz]*m3;
                i4 = input[(i)  + j  * input_sz]*m4;
                i5 = input[(i+1)+(j)  *input_sz]*m5;
                i6 = input[(i-1)+(j+1)*input_sz]*m6;
                i7 = input[(i)  +(j+1)*input_sz]*m7;
                i8 = input[(i+1)+(j+1)*input_sz]*m8;
                output[ind] = i0 + i1 + i2 + i3 + i4 + i5 + i6 + i7 + i8;
            } else {
                output[ind] = input[ind];
            }
        }
    }
}
