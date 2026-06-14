// See LICENSE for license details.

//**************************************************************************
// Median filter (c version)
//--------------------------------------------------------------------------

#include <stdlib.h>
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

void __attribute__((noinline)) mt_csaxpy( 
    int cid, int nc,
    int n, int a,
    int input_X[], int inout_Y[], char cond[] )
{
  const size_t chunk_sz = n / nc;
  const size_t chunk = cid * chunk_sz;
  
  for (size_t i = chunk; i < MIN(chunk+chunk_sz, n); i++) {
    if (cond[i])
      inout_Y[i] += a * input_X[i];
  }
}
