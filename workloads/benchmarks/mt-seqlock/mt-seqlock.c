//**************************************************************************
// mt-seqlock: targeted seqlock stress microbenchmark
//--------------------------------------------------------------------------
// Mirrors the Linux kernel's timekeeping seqlock (tk_core.seq): a single
// writer bumps an odd/even sequence counter around updates to a protected
// value; every other core spins re-reading (seq, data, seq) until it sees a
// consistent (even, unchanged) snapshot -- the exact pattern chiron's SMP
// Linux boot wedges on inside ktime_get()/ktime_get_update_offsets_now(), but
// exercised thousands of times here instead of once per timer tick, to
// reproduce a cross-core store-visibility race in a small cycle budget
// instead of a ~600M-cycle kernel boot.
//
// Two independent, BOUNDED failure detectors (so this benchmark always
// terminates, unlike the real kernel hang):
//   RETRY_CAP -- a read never becomes internally consistent (seq odd, or
//                changed between the two reads) within this many attempts.
//   STALE_CAP -- reads ARE internally consistent but the observed value
//                never changes for this many consecutive iterations, i.e.
//                the reader is happily consuming a frozen, stale snapshot
//                forever -- a subtler form of the same bug that a naive
//                consistency check alone would miss.
//--------------------------------------------------------------------------
#include "util.h"

#define ITERS      10000
#define RETRY_CAP  200000
#define STALE_CAP  10000

extern void __attribute__((noinline)) barrier(int ncores);
extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

// seq/data deliberately adjacent (same cache line), mirroring struct
// tk_core's seqcount sitting next to the protected timekeeper fields.
static volatile int seq  = 0;
static volatile int data = 0;

#define MAX_CORES 4
static volatile int wedge_kind[MAX_CORES];   // 0 none, 1 RETRY, 2 STALE
static volatile int wedge_iter[MAX_CORES];
static volatile int wedge_extra[MAX_CORES];  // retries, or stale streak length

void thread_entry(int cid, int nc)
{
  initialize_count_asm(0);
  wedge_kind[cid]  = 0;
  wedge_iter[cid]  = 0;
  wedge_extra[cid] = 0;

  barrier(nc);   // start together

  if (cid == 0) {
    for (int i = 0; i < ITERS; i++) {
      seq++;                  // odd: write in progress
      __sync_synchronize();
      data = i;                // protected value, monotonically increasing
      __sync_synchronize();
      seq++;                  // even: write complete
    }
  } else {
    int last_d = -1;
    int same_streak = 0;
    for (int i = 0; i < ITERS; i++) {
      int s1, s2, d, retries = 0;
      do {
        s1 = seq;
        __sync_synchronize();
        d = data;
        __sync_synchronize();
        s2 = seq;
        if (++retries > RETRY_CAP) {
          wedge_kind[cid]  = 1;
          wedge_iter[cid]  = i;
          wedge_extra[cid] = retries;
          break;
        }
      } while ((s1 & 1) || (s1 != s2));
      if (wedge_kind[cid]) break;

      if (d == last_d) {
        if (++same_streak > STALE_CAP) {
          wedge_kind[cid]  = 2;
          wedge_iter[cid]  = i;
          wedge_extra[cid] = same_streak;
          break;
        }
      } else {
        same_streak = 0;
        last_d = d;
      }
    }
  }

  barrier(nc);   // rendezvous before verdict

  if (cid == 0) {
    int any_wedge = 0;
    for (int c = 1; c < nc; c++) {
      if (wedge_kind[c]) {
        any_wedge = 1;
        uart_send_string(wedge_kind[c] == 1 ? "RETRY-WEDGE core " : "STALE-WEDGE core ");
        uart_send_integer(c);
        uart_send_string(" iter ");
        uart_send_integer(wedge_iter[c]);
        uart_send_string(" extra ");
        uart_send_integer(wedge_extra[c]);
        uart_send_string("\n");
      }
    }
    if (!any_wedge) uart_send_string("mt-seqlock: ALL READERS OK\n");
    exit(any_wedge);
  } else {
    exit(2);
  }
}
