//**************************************************************************
// util.h
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 22/03/2025
//
//
//--------------------------------------------------------------------------


//--------------------------------------------------------------------------
// gards and includes


#ifndef __UTIL_H
#define __UTIL_H

#include <stdint.h>

// Atomic fetch-and-add using RISC-V atomic instruction
static inline int atomic_fetch_add(volatile int *ptr, int val)
{
  int old;
  asm volatile(
      "amoadd.w.aqrl %0, %2, (%1)" // Atomic add with acquire-release semantics
      : "=r"(old)
      : "r"(ptr), "r"(val)
      : "memory");
  return old;
}


static void __attribute__((noinline)) barrier(int ncores)
{
  static volatile int sense =0 ;
  static volatile int count =0;
  static __thread int threadsense = 0; // non shared variable in TLS

  __sync_synchronize(); // fence

  threadsense = !threadsense;

  int old_count = atomic_fetch_add(&count, 1); // Atomically increment count
  if (old_count == ncores - 1)
  {
    // Last thread to arrive
    count = 0;           // Reset counter for next barrier
    sense = threadsense; // Update global sense to this thread's sense
  }
  else
  {
    // Other threads wait for global sense to match their local sense
    while (sense != threadsense)
    {
      // Busy wait (could optimize with a yield if available)
    }
  }

  __sync_synchronize();
}


#endif //__UTIL_H
