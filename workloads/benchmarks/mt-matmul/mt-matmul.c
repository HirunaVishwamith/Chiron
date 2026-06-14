//**************************************************************************
// Multi-threaded Matrix Multiply benchmark
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 10/03/2025
//
//
// This benchmark multiplies two 2-D arrays together and writes the results to
// a third vector. The input data (and reference data) should be generated
// using the matmul_gendata.pl perl script and dumped to a file named
// dataset.h.

// print out arrays, etc.
#define DEBUG

//--------------------------------------------------------------------------
// Includes

//--------------------------------------------------------------------------
// Input/Reference Data

#include "dataset.h"
// #include "s3.h"

//--------------------------------------------------------------------------
// Basic Utilities and Multi-thread Support

#include "util.h"

//--------------------------------------------------------------------------
// external functions

extern void __attribute__((noinline)) matmul(const int coreid, const int ncores, const int lda, const data_t A[], const data_t B[], data_t C[]);

extern void uart_send_string(const char *s);

extern void syscall_print_matrix(int lda, int *matrix);

extern void uart_send_integer(int n);

extern void exit(int status);

//--------------------------------------------------------------------------
// Main
//
// all threads start executing thread_entry(). Use their "coreid" to
// differentiate between threads (each thread is running on a separate core).

void thread_entry(int cid, int nc)
{
    static data_t results_data[ARRAY_SIZE] = {0};
    initialize_count_asm(0);


    //     stats(matmul(cid, nc, DIM_SIZE, input1_data, input2_data, results_data); barrier(nc), DIM_SIZE/DIM_SIZE/DIM_SIZE); // this needs to be enablled to calculate IPC
    // for that mcycle and mretinstr csr registers has to be enable in the hardware

    matmul(cid, nc, DIM_SIZE, input1_data, input2_data, results_data);
    barrier(nc);

    

    // Call the syscall to print the matrix to UART
    if (cid == 0)
    { // Optional: Only core 0 prints to avoid potential output issues in simple setup
        uart_send_string("The code is ran with error code: ");
        int res = verify(ARRAY_SIZE, results_data, verify_data);
        uart_send_integer(res);
        uart_send_string("\n");
        #ifdef DEBUG
        syscall_print_matrix(DIM_SIZE, results_data);
        #endif
    }

    exit(2); // two threads will loop here onwards
}

// void thread_entry(int cid, int nc)
// {
//       uart_send_string("Hello world"); // Print the hart ID <- this print function is defined in syscalls.c
//       exit(2);    // two threads will loop here onwards
// }
