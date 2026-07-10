//**************************************************************************
// mt-divirq: M-extension divide clusters under timer-interrupt fire
//--------------------------------------------------------------------------
// mt-divburst proved back-to-back divides work with interrupts quiet. Linux
// still wedged at __update_load_avg_cfs_rq's divuw (0x8024bdd8) after ~150M
// cycles — signature: ROB head parked at a divide forever while other harts
// run. Hypothesis: a timer-interrupt pipeline flush landing in the small
// window between a divide's scheduler release and its divider arm strands
// the M-extension busy gate (mExtensionReady) closed, so no later divide is
// ever released.
//
// This benchmark re-arms the CLINT timer every 200 mtime ticks (~3200
// cycles) from a tiny custom mtvec handler and runs divide clusters
// continuously; the IRQ phase sweeps across the clusters, so the
// release-window collision is hit within a couple million cycles if the
// hole exists. Self-checks the same checksum as mt-divburst.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define ITERS 20000

static volatile unsigned long seed64 = 0x9e3779b97f4a7c15UL;
volatile unsigned long divirq_count = 0;

// Minimal M-mode timer handler: bump the IRQ counter, re-arm
// mtimecmp[mhartid] = mtime + 200, mret. Clobbers nothing.
__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global divirq_handler\n"
    "divirq_handler:\n"
    "  addi sp, sp, -48\n"
    "  sd t0, 0(sp)\n"
    "  sd t1, 8(sp)\n"
    "  sd t2, 16(sp)\n"
    "  sd t3, 24(sp)\n"
    "  la t0, divirq_count\n"
    "  ld t1, 0(t0)\n"
    "  addi t1, t1, 1\n"
    "  sd t1, 0(t0)\n"
    "  csrr t0, mhartid\n"
    "  slli t0, t0, 3\n"
    "  li t1, 0x02004000\n"
    "  add t1, t1, t0\n"
    "  li t2, 0x0200bff8\n"
    "  ld t3, 0(t2)\n"
    "  addi t3, t3, 200\n"
    "  sd t3, 0(t1)\n"
    "  ld t0, 0(sp)\n"
    "  ld t1, 8(sp)\n"
    "  ld t2, 16(sp)\n"
    "  ld t3, 24(sp)\n"
    "  addi sp, sp, 48\n"
    "  mret\n"
    ".popsection\n");

static inline void divirq_arm(void)
{
  __asm__ volatile(
      "la t0, divirq_handler\n\t"
      "csrw mtvec, t0\n\t"
      "csrr t0, mhartid\n\t"
      "slli t0, t0, 3\n\t"
      "li t1, 0x02004000\n\t"
      "add t1, t1, t0\n\t"
      "li t2, 0x0200bff8\n\t"
      "ld t3, 0(t2)\n\t"
      "addi t3, t3, 200\n\t"
      "sd t3, 0(t1)\n\t"
      "li t0, 0x80\n\t"      // mie.MTIE
      "csrs mie, t0\n\t"
      "csrsi mstatus, 8\n\t" // mstatus.MIE
      ::: "t0", "t1", "t2", "t3", "memory");
}

static inline void divirq_disarm(void)
{
  __asm__ volatile(
      "csrci mstatus, 8\n\t"
      "li t0, 0x80\n\t"
      "csrc mie, t0\n\t"
      ::: "t0", "memory");
}

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
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  barrier(nc);

  // ALL participating harts run the div+IRQ loop (in the q4 build this adds
  // the multi-hart bus/coherency traffic the Linux wedge context has); only
  // hart 0 reports.
  unsigned long acc_asm = 0, acc_c = 0;
  unsigned long x = seed64 + (unsigned long)cid * 0x1234567UL;

  divirq_arm();

  for (int i = 1; i <= ITERS; i++) {
    unsigned long a = x ^ (x >> 7);
    unsigned long b = (unsigned long)((i & 0xffff) * 977 + 13);
    unsigned int  c = (unsigned int)(a >> 11) | 1u;
    unsigned int  d = (unsigned int)(i * 31 + 7);

    unsigned long q0, q1, r0;
    unsigned long spill;
    unsigned int  q2, q3;

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

    {
      unsigned long rq0 = divu64(a, b);
      acc_c += rq0 + remu64(a, b) + divu32(c, d) + divu32(d, c) +
               divu64(a, rq0);
    }

    // Branch-shadow cluster: the beqz condition depends on the first divu,
    // so it cannot resolve for ~65 cycles — the shadow divides behind it are
    // released speculatively and get flushed on the ~50% mispredicts, with
    // timer IRQs (whose injection forges a branch mispredict to flush
    // speculated instructions) landing on top. This is the missing Linux
    // ingredient mt-divburst's straight-line clusters lack.
    {
      unsigned long s0, s1, s2, srem;
      unsigned long c64 = (unsigned long)c | 1UL;
      asm volatile(
          "divu  %[s0], %[a], %[b]\n\t"
          "li    %[s1], 0\n\t"
          "li    %[s2], 0\n\t"
          "andi  %[sr], %[s0], 1\n\t"
          "beqz  %[sr], 1f\n\t"
          "divu  %[s1], %[a], %[c64]\n\t"
          "j     2f\n\t"
          "1:\n\t"
          "divuw %[s2], %[c], %[d]\n\t"
          "2:\n\t"
          "remu  %[sr], %[a], %[b]\n\t"
          : [s0] "=&r"(s0), [s1] "=&r"(s1), [s2] "=&r"(s2), [sr] "=&r"(srem)
          : [a] "r"(a), [b] "r"(b), [c] "r"(c), [d] "r"(d), [c64] "r"(c64)
          :);
      acc_asm += s0 + s1 + s2 + srem;

      unsigned long rs0 = divu64(a, b);
      unsigned long rs1 = 0, rs2 = 0;
      if (rs0 & 1)
        rs1 = divu64(a, c64);
      else
        rs2 = (unsigned long)(long)(int)divu32(c, d);  // divuw sign-extends rd
      acc_c += rs0 + rs1 + rs2 + remu64(a, b);
    }

    x = x * 6364136223846793005UL + 1442695040888963407UL;
  }

  divirq_disarm();
  barrier(nc);

  if (cid != 0)
    exit(2);

  uart_send_string("mt-divirq: irqs=");
  uart_send_integer((int)divirq_count);
  uart_send_string("\n");

  if (acc_asm == acc_c && divirq_count > 100) {
    uart_send_string("mt-divirq: PASS\n");
    exit(0);
  } else if (divirq_count <= 100) {
    uart_send_string("mt-divirq: FAIL too few irqs\n");
    exit(3);
  } else {
    uart_send_string("mt-divirq: FAIL checksum\n");
    exit(1);
  }
}
