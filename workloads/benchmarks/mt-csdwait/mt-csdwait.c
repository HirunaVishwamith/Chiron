//**************************************************************************
// mt-csdwait: does a spin-on-load loop containing cpu_relax() ever observe a
//             peer hart's store?
//--------------------------------------------------------------------------
// Quad-core Linux reaches "Run /init as init process" and then hangs with
// hart1 parked in smp_call_function_many_cond+0x2e4 for 800M+ cycles while
// harts 0/2/3 cycle through the idle loop. Nothing is wedged — every hart
// advances, the ROB is healthy — so this is a visibility/wakeup failure, not
// the pipeline wedge that was fixed earlier.
//
// That PC is csd_lock_wait, and its loop is:
//
//   802952c8:  div    a5,a5,zero     <- cpu_relax() on RISC-V IS a div-by-zero
//   802952cc:  fence  w,unknown
//   802952d0:  lw     a5,8(a4)       <- reload csd->node.u_flags
//   802952d4:  andi   a5,a5,1        <- test CSD_FLAG_LOCK
//   802952d8:  bnez   a5,802952c8    <- loop back onto the divide
//
// mt-spinwait already proves a plain spin-on-load sees a peer's store, and it
// passes. mt-ipiwfi proves IPIs are delivered even to a hart idling on WFI, and
// it passes. The one thing csd_lock_wait does that neither covers is putting a
// LONG-LATENCY DIVIDE inside the spin loop, with the reload speculated past a
// branch that jumps straight back onto that divide, every iteration, for
// hundreds of millions of iterations.
//
// This isolates precisely that, with no IPIs involved, so it separates the two
// remaining explanations:
//   * this test HANGS  -> the spinning hart never observes the peer's store:
//                         a stale-load / coherent-invalidation bug, and the
//                         Linux hang needs no IPI explanation at all.
//   * this test PASSES -> visibility is fine, so the fault is upstream: the
//                         target CPU never runs the csd callback (IPI queueing
//                         or the ipi_mux demux layer).
//
// Build both ways for a clean A/B:
//   -DCSD_RELAX=1 (default) spin loop contains `div x,x,zero`, as Linux does
//   -DCSD_RELAX=0           plain spin loop — the mt-spinwait-equivalent control
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef CSD_RELAX
#define CSD_RELAX 1
#endif

#define ROUNDS      64
#define WAIT_CAP    4000000UL
// Make the observer wait a while before clearing, so the spinner is deep inside
// its divide loop when the store lands — the Linux case, not a lucky first pass.
#define CLEAR_DELAY 500UL

// Each flag on its own 64-byte line: this is about coherence of ONE line, not
// false sharing between them.
typedef struct {
  volatile unsigned int flag;
  unsigned char pad[60];
} csd_slot_t;

static csd_slot_t csd[4] __attribute__((aligned(64)));
static volatile unsigned long rounds_done[4];
static volatile int fail_hart[4];

// cpu_relax() exactly as the RISC-V kernel emits it.
static inline void cpu_relax_riscv(void)
{
#if CSD_RELAX
  int dummy;
  __asm__ __volatile__("div %0, %0, zero" : "=r"(dummy));
#endif
  __asm__ __volatile__("" ::: "memory");
}

// csd_lock_wait: spin until the peer clears bit 0. Returns 0 on success,
// 1 if it gave up — a hang here IS the bug being hunted.
static int csd_lock_wait(volatile unsigned int *flag)
{
  for (unsigned long spin = 0; (*flag & 1u) != 0u; spin++) {
    cpu_relax_riscv();
    if (spin > WAIT_CAP)
      return 1;
  }
  return 0;
}

void thread_entry(int cid, int nc)
{
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  csd[cid].flag = 0;
  rounds_done[cid] = 0;
  fail_hart[cid] = 0;
  barrier(nc);

  if (cid == 0) {
    // Observer: for each round, wait a bit so the spinner is well inside its
    // divide loop, then clear its flag. Exactly the shape of a remote CPU
    // finishing a cross-call and releasing the csd.
    for (unsigned long r = 1; r <= ROUNDS; r++) {
      for (int t = 1; t < nc; t++) {
        // Wait until the target has armed its flag for this round.
        for (unsigned long s = 0; csd[t].flag == 0u; s++)
          if (s > WAIT_CAP) {
            fail_hart[t] = 1;
            goto done;
          }
        for (volatile unsigned long d = 0; d < CLEAR_DELAY; d++)
          ;
        __sync_synchronize();
        csd[t].flag = 0;   // release, as the callback would
        __sync_synchronize();
      }
      rounds_done[0] = r;
    }
  } else {
    for (unsigned long r = 1; r <= ROUNDS; r++) {
      __sync_synchronize();
      csd[cid].flag = 1;          // csd_lock()
      __sync_synchronize();
      if (csd_lock_wait(&csd[cid].flag)) {
        fail_hart[cid] = 1;
        break;
      }
      rounds_done[cid] = r;
    }
  }

done:
  barrier(nc);

  if (cid != 0)
    exit(2);

  int ok = 1;
  for (int t = 1; t < nc; t++) {
    ok &= (fail_hart[t] == 0);
    ok &= (rounds_done[t] == ROUNDS);
  }

  uart_send_string("mt-csdwait: relax=");
  uart_send_string(CSD_RELAX ? "div" : "none");
  uart_send_string(" rounds=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rounds_done[i]);
    uart_send_string(i == 3 ? " fail=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer(fail_hart[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  if (ok) {
    uart_send_string("mt-csdwait: PASS\n");
    exit(0);
  }
  uart_send_string("mt-csdwait: FAIL (spinner never observed the release)\n");
  exit(1);
}
