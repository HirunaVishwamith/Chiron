//**************************************************************************
// mt-fencei: does a release-store issued right after fence.i stay visible?
//--------------------------------------------------------------------------
// Forensics on the quad-core Linux /init hang (linux_csd_probe, restored from
// ckpt_001100000000.bin) produced this state:
//
//   stuck csd @ 87dd6b80   u_flags = 00000011   (CSD_FLAG_LOCK | CSD_TYPE_SYNC)
//   node.llist.next = 0
//   call_single_queue heads:  cpu0..cpu3 all 0   (every queue EMPTY)
//   func = 802054e8 = ipi_remote_fence_i
//   slot1 and slot2 both stuck; slot0 and slot3 free
//   hart0: 50 commits in handle_IPI, 100 in __flush_smp_call_function_queue
//          per 1M cycles — it keeps servicing IPIs and finding nothing queued
//   hart1 reads 0x00000011; DRAM holds 0x00000011 — identical, so the spinning
//          hart is NOT holding a stale line
//
// The queues being empty while the flags stay locked means the entries were
// dequeued: a target popped the csd, ran the callback, and then csd_unlock()'s
//   smp_store_release(&csd->node.u_flags, 0)
// never became visible. Nothing is wedged — every hart still commits.
//
// The callback is ipi_remote_fence_i, which is a bare `fence.i`. On this RTL
// fence.i arms the D-cache clean-on-fence walker, which sweeps dirty lines back
// to L2. So the untested sequence is precisely:
//
//     dirty some lines  ->  fence.i (walker sweeps)  ->  release-store  ->  peer polls
//
// mt-csdwait already proves the poll/release pair works on its own and it
// PASSES; it has no fence.i, which is exactly the gap this fills. mt-spinwait
// and mt-ipiwfi likewise cover the spin and the IPI delivery but never the
// walker.
//
// A/B knobs, so a failure is attributed rather than merely observed:
//   -DFENCEI=1      (default) target executes fence.i before releasing
//   -DFENCEI=0      control: identical program with the fence.i removed
//   -DDIRTY_LINES=n how many dirty D-cache lines the walker has to sweep
//
//   FENCEI=1 hangs / FENCEI=0 passes -> the walker loses the release store.
//   both pass                        -> the walker is exonerated; the lost
//                                       store is elsewhere in csd_unlock's path.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef FENCEI
#define FENCEI 1
#endif

// Enough lines to guarantee the walker has real work and that the released line
// is not trivially the most-recent one it touches.
#ifndef DIRTY_LINES
#define DIRTY_LINES 64
#endif

#define ROUNDS   64
#define WAIT_CAP 60000UL

// One 64-byte line each: this is about one line's coherence, not false sharing.
typedef struct {
  volatile unsigned int word;     // the release variable (csd->node.u_flags)
  volatile unsigned int scratch;  // SAME LINE, written before the fence.i
  unsigned char pad[56];
} line_t;

static line_t csd[4]      __attribute__((aligned(64)));  // the release variable
static line_t doorbell[4] __attribute__((aligned(64)));  // stands in for the IPI
static line_t dirty[4][DIRTY_LINES] __attribute__((aligned(64)));

static volatile unsigned long rounds_done[4];
static volatile int fail_hart[4];

// cpu_relax() exactly as the RISC-V kernel emits it — the divide is part of the
// shape csd_lock_wait actually has.
static inline void cpu_relax_riscv(void)
{
  int dummy;
  __asm__ __volatile__("div %0, %0, zero" : "=r"(dummy));
  __asm__ __volatile__("" ::: "memory");
}

static inline void local_flush_icache_all(void)
{
#if FENCEI
  __asm__ __volatile__("fence.i" ::: "memory");
#endif
}

// csd_lock_wait(): spin until the peer clears bit 0. 1 = gave up (the bug).
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
  csd[cid].word      = 0;
  csd[cid].scratch   = 0;
  doorbell[cid].word = 0;
  rounds_done[cid]   = 0;
  fail_hart[cid]     = 0;
  barrier(nc);

  if (cid == 0) {
    // Sender: lock every target's csd, ring its doorbell, then spin on each
    // flag — the shape of smp_call_function_many_cond with wait=1.
    for (unsigned long r = 1; r <= ROUNDS; r++) {
      for (int t = 1; t < nc; t++) {
        csd[t].word = 0x11u;          // CSD_FLAG_LOCK | CSD_TYPE_SYNC
        __sync_synchronize();
        doorbell[t].word = 1u;        // send_call_function_single_ipi()
      }
      for (int t = 1; t < nc; t++) {
        if (csd_lock_wait(&csd[t].word)) {
          fail_hart[t] = 1;
          goto done;
        }
      }
      rounds_done[0] = r;
    }
  } else {
    // Target: wait for the doorbell, dirty the D-cache, run the callback
    // (fence.i), then release the csd — __flush_smp_call_function_queue's
    // csd->func(info) followed by csd_unlock(csd).
    for (unsigned long r = 1; r <= ROUNDS; r++) {
      for (unsigned long s = 0; doorbell[cid].word == 0u; s++)
        if (s > WAIT_CAP) {
          fail_hart[cid] = 1;
          goto done;
        }
      doorbell[cid].word = 0u;

      for (int i = 0; i < DIRTY_LINES; i++)
        dirty[cid][i].word = (unsigned int)(r + i);

      // Dirty the RELEASE LINE ITSELF before the fence, which is what Linux
      // does and what the first version of this test missed: the servicing
      // hart runs llist_reverse_order() and writes entry->next at offset 0 of
      // the very cache line whose offset 8 it is about to release. So the
      // walker sweeps THAT line, not merely unrelated ones.
      csd[cid].scratch = (unsigned int)r;

      local_flush_icache_all();       // ipi_remote_fence_i()

      __sync_synchronize();           // smp_store_release(): fence rw,w
      csd[cid].word = 0u;             // csd_unlock()

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
  ok &= (rounds_done[0] == ROUNDS);

  uart_send_string("mt-fencei: fencei=");
  uart_send_integer(FENCEI);
  uart_send_string(" dirty=");
  uart_send_integer(DIRTY_LINES);
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
    uart_send_string("mt-fencei: PASS\n");
    exit(0);
  }
  uart_send_string("mt-fencei: FAIL (release store after fence.i never seen)\n");
  exit(1);
}
