//**************************************************************************
// mt-divburst: back-to-back M-extension divide regression
//--------------------------------------------------------------------------
// Reproduces the Linux SMP boot wedge in __update_load_avg_se
// (kernel 6.3, 0x8024bb00..0x8024bb0c):
//
//     divu  a5,a5,a7
//     divu  a6,a6,a7
//     sd    a5,224(a2)
//     divuw a5,a3,a4      <-- ROB head parks here forever
//
// The divider's "already completed" latch (divDone in core.scala) only
// cleared when the execute-stage request went invalid or stopped being a
// divide. Two divides passing through prf.toExec with no non-divide gap
// left divDone set, so the second divide never armed the divider and the
// core wedged. This benchmark issues clusters of adjacent divu/divuw/remu
// (independent and dependent, with and without an intervening store) so a
// single hart exercises every adjacency the scheduler can produce.
//
// Self-checking: the asm cluster's results are accumulated and compared
// against the same math done in plain C (helpers below mirror RISC-V
// divide-by-zero semantics, so any divisor value is legal). Under
// lockstep the golden model additionally checks every commit.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define ITERS 512

static volatile unsigned long seed64 = 0x9e3779b97f4a7c15UL;

// RISC-V M semantics incl. divide-by-zero (quotient all-ones, rem dividend).
static inline unsigned long divu64(unsigned long n, unsigned long d)
{
  return d ? n / d : ~0UL;
}
static inline unsigned long remu64(unsigned long n, unsigned long d)
{
  return d ? n % d : n;
}
static inline unsigned int divu32(unsigned int n, unsigned int d)
{
  return d ? n / d : ~0u;
}

void thread_entry(int cid, int nc)
{
  // crt.S starts ALL RTL harts in thread_entry regardless of -DNUM_CORES.
  // Harts beyond nc must park before touching the barrier state: under the
  // single-core lockstep the golden model runs one hart only, and a stray
  // amoadd on `count` desyncs core 0's barrier read (x14 mismatch).
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  barrier(nc);

  if (cid != 0) {
    barrier(nc);
    exit(2);
  }

  unsigned long acc_asm = 0, acc_c = 0;
  unsigned long x = seed64;

  for (int i = 1; i <= ITERS; i++) {
    unsigned long a = x ^ (x >> 7);
    unsigned long b = (unsigned long)(i * 977 + 13);
    unsigned int  c = (unsigned int)(a >> 11) | 1u;
    unsigned int  d = (unsigned int)(i * 31 + 7);

    unsigned long q0, q1, r0;
    unsigned long spill;
    unsigned int  q2, q3;

    // Exact kernel shape: divu / divu / sd / divuw, then a dependent
    // divide (quotient feeds the next divisor) and a remu chased by a
    // divuw with zero gap.
    asm volatile(
        "divu  %[q0], %[a], %[b]\n\t"
        "divu  %[q1], %[b], %[a]\n\t"
        "sd    %[q0], 0(%[sp])\n\t"
        "divuw %[q2], %[c], %[d]\n\t"
        "divu  %[r0], %[a], %[q0]\n\t"
        "remu  %[q1], %[a], %[b]\n\t"
        "divuw %[q3], %[d], %[c]\n\t"
        : [q0] "=&r"(q0), [q1] "=&r"(q1), [q2] "=&r"(q2),
          [q3] "=&r"(q3), [r0] "=&r"(r0)
        : [a] "r"(a), [b] "r"(b), [c] "r"(c), [d] "r"(d),
          [sp] "r"(&spill)
        : "memory");

    acc_asm += q0 + q1 + q2 + q3 + r0;

    // Same cluster in plain C. Note q1's first result (b/a) is dead in the
    // asm block -- the remu overwrites it -- so it does not enter the sum.
    {
      unsigned long rq0 = divu64(a, b);
      acc_c += rq0 + remu64(a, b) + divu32(c, d) + divu32(d, c) +
               divu64(a, rq0);
    }

    x = x * 6364136223846793005UL + 1442695040888963407UL;
  }

  barrier(nc);

  if (acc_asm == acc_c) {
    uart_send_string("mt-divburst: PASS\n");
    exit(0);
  } else {
    uart_send_string("mt-divburst: FAIL checksum\n");
    exit(1);
  }
}
