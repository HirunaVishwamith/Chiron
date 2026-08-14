//**************************************************************************
// mt-spinwait: reproduce Linux secondary CPU park/wake (cpu_ops_spinwait)
//--------------------------------------------------------------------------
// Losers spin:
//   while (!stack_ptr[hart] || !task_ptr[hart]) ;
// Winner (hart 0) after barrier writes those pointers (release), then
// waits for each secondary to set a per-hart "online" flag.
//
// This is the exact protocol at clear_bss_done+0x7c in Linux head.S —
// NOT a seqlock. If stores are invisible, secondaries spin forever and
// primary times out (Linux: "CPU N: failed to come online").
//**************************************************************************
#include "util.h"

#define MAX_CORES 4
#define WAIT_CAP  2000000

extern void __attribute__((noinline)) barrier(int ncores);
extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

// Per-hart slots (like __cpu_spinwait_stack/task_pointer)
static volatile unsigned long stack_ptr[MAX_CORES];
static volatile unsigned long task_ptr[MAX_CORES];
static volatile int online[MAX_CORES];
static volatile int fail_hart[MAX_CORES];

void thread_entry(int cid, int nc)
{
  int i;
  initialize_count_asm(0);
  stack_ptr[cid] = 0;
  task_ptr[cid]  = 0;
  online[cid]    = 0;
  fail_hart[cid] = 0;

  barrier(nc);

  if (cid == 0) {
    // Primary: publish wake data for each secondary (hart 1..nc-1)
    __sync_synchronize();
    for (i = 1; i < nc; i++) {
      // Distinct non-zero payloads (stand-ins for stack top / task_struct*)
      stack_ptr[i] = 0x80010000UL + (unsigned long)(i * 0x1000);
      task_ptr[i]  = 0x80020000UL + (unsigned long)(i * 0x1000);
    }
    __sync_synchronize();

    // Wait for secondaries to ack
    for (i = 1; i < nc; i++) {
      int spins = 0;
      while (!online[i]) {
        if (++spins > WAIT_CAP) {
          fail_hart[i] = 1;
          break;
        }
      }
    }

    barrier(nc);

    {
      int any = 0;
      for (i = 1; i < nc; i++) {
        if (fail_hart[i] || !online[i]) {
          any = 1;
          uart_send_string("SPINWAIT-FAIL hart ");
          uart_send_integer(i);
          uart_send_string(" online=");
          uart_send_integer(online[i]);
          uart_send_string("\n");
        }
      }
      if (!any)
        uart_send_string("mt-spinwait: ALL SECONDARIES WOKE\n");
      exit(any);
    }
  } else {
    // Secondary: park until both pointers visible (Linux head.S pattern)
    unsigned long sp, tp;
    int spins = 0;
    do {
      sp = stack_ptr[cid];
      tp = task_ptr[cid];
      if (++spins > WAIT_CAP) {
        fail_hart[cid] = 1;
        break;
      }
    } while (sp == 0 || tp == 0);

    if (sp != 0 && tp != 0) {
      __sync_synchronize();
      online[cid] = 1;
    }

    barrier(nc);
    exit(2); // secondaries don't report
  }
}
