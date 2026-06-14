//**************************************************************************
// Masked Gaussian Blur benchmark
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 10/03/2025
//
//--------------------------------------------------------------------------

// to print out arrays, etc.
#define DEBUG

//--------------------------------------------------------------------------
// Includes

//--------------------------------------------------------------------------
// Input/Reference Data

#include "dataset2.h"
// #include "s3.h"


//--------------------------------------------------------------------------
// Basic Utilities and Multi-thread Support

#include "util.h"

//--------------------------------------------------------------------------
// vvadd function

extern void __attribute__((noinline)) mask_sfilter(int coreid, int ncores,
                                                   const data_t *input, const data_t *output, short *mask, int input_sz,
                                                   data_t m0, data_t m1, data_t m2, data_t m3, data_t m4, data_t m5,
                                                   data_t m6, data_t m7, data_t m8);

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
  // static allocates data in the binary, which is visible to both threads
  static data_t results_data[DATA_SIZE * DATA_SIZE]={0};
  initialize_count_asm(0);

  int m0 = 1;
  int m1 = 1;
  int m2 = 1;
  int m3 = 1;
  int m4 = 1;
  int m5 = 1;
  int m6 = 1;
  int m7 = 1;
  int m8 = 1;

  mask_sfilter(cid, nc, input, results_data, mask, DATA_SIZE, m0, m1, m2, m3, m4, m5, m6, m7, m8);
  barrier(nc);

  if (cid == 0)
  {
    uart_send_string("The code is ran with error code: ");
    int res = verify(DATA_SIZE * DATA_SIZE, results_data, verify_data);
    uart_send_integer(res);
    uart_send_string("\n");
#ifdef DEBUG
    syscall_print_matrix(DATA_SIZE, results_data);
#endif
    // we assume that results_data edges are zeroed out to start with
  }

  exit(2);
}
