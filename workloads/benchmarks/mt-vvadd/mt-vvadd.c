//**************************************************************************
// Vector-vector add benchmark
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 10/03/2025
//
// This benchmark adds two vectors and writes the results to a
// third vector. The input data (and reference data) should be
// generated using the vvadd_gendata.pl perl script and dumped
// to a file named dataset.h

// to print out arrays, etc.
#define DEBUG

//--------------------------------------------------------------------------
// Includes

//--------------------------------------------------------------------------
// Input/Reference Data

#include "dataset2.h"

//--------------------------------------------------------------------------
// Basic Utilities and Multi-thread Support

#include "util.h"

//--------------------------------------------------------------------------
// external functions

extern void __attribute__((noinline)) vvadd(int coreid, int ncores, int n, const data_t *x, const data_t *y, data_t *z);

extern void uart_send_string(const char *s);

extern void syscall_print_array(int lda, int *array);

extern void uart_send_integer(int n);

extern void exit(int status);

//--------------------------------------------------------------------------
// Main
//
// all threads start executing thread_entry(). Use their "coreid" to
// differentiate between threads (each thread is running on a separate core).

void thread_entry(int cid, int nc)
{
     // static allocates data in the binary, which is visible to both threads
     static data_t results_data[DATA_SIZE] = {0};
     initialize_count_asm(0);
     // initialize_sense_asm(0);

     vvadd(cid, nc, DATA_SIZE, input_data_X, input_data_Y, results_data);
     barrier(nc);

     

     // Call the syscall to print the matrix to UART
     if (cid == 0)
     { // Optional: Only core 0 prints to avoid potential output issues in simple setup
          uart_send_string("The code is ran with error code: ");
          int res = verify(DATA_SIZE, results_data, verify_data_Z);
          uart_send_integer(res);
          uart_send_string("\n");
          #ifdef DEBUG
          syscall_print_array(DATA_SIZE, results_data);
          #endif
     }

     exit(2); // two threads will loop here onwards

}
