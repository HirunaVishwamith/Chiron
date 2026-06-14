// See LICENSE for license details.

//**************************************************************************
// Vector SAXPY benchmark for Hwacha v4
//--------------------------------------------------------------------------
//
#include <stdbool.h>
// #include <stdlib.h>
#include "util.h"
#include "mt-csaxpy.h"

//--------------------------------------------------------------------------
// Input/Reference Data

#include "dataset1.h"

#define DEBUG

extern void uart_send_string(const char *s);

extern void uart_send_integer(int n);

extern void syscall_print_array(int lda, int *array);

extern void exit(int status);

//--------------------------------------------------------------------------
// Main

int thread_entry(int cid, int nc)
{

  initialize_count_asm(0);

  // Do SAXPY
  mt_csaxpy(cid, nc, DATA_SIZE, input_data_a, input_data_X, input_data_Y, cond_data);
  barrier(nc);

  if (cid == 0)
  {

    // Check the results
    uart_send_string("The code is ran with error code: ");
    int res = verify(DATA_SIZE, input_data_Y, verify_data);
    uart_send_integer(res);
    uart_send_string("\n");

    #ifdef DEBUG
      syscall_print_array(DATA_SIZE, input_data_Y);
    #endif
  }

  exit(0);
}
