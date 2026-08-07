// mt-stress.c — GENERATED, do not edit by hand.
//
// Constrained-random speculation stress program.
//   seed   = 1
//   blocks = 120
//   cores  = 4
// Regenerate with:  tools/gen_stress.py --seed 1 --blocks 120
//
// This program computes nothing meaningful. Its value is the instruction
// sequences it puts the core into: divides and remainders issued inside the
// shadow of data-dependent (therefore unpredictable) branches, speculative MMIO
// reads, and cross-hart AMO/LR-SC contention. Correctness is judged externally
// — by lock-step against the golden emulator (single core), or by the
// microarchitectural assertions in sim/harness/invariants.h (quad core).
//
// The accumulator is carried in a volatile-ish chain so the compiler cannot
// fold the blocks away; every block's result feeds the next block's operands,
// which also means a single wrong speculative result propagates and is caught.

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define SCRATCH_WORDS 256
#define SHARED_WORDS  16

// Per-hart scratch (no false sharing between harts: one 64-byte line each way)
static volatile unsigned long scratch[4][SCRATCH_WORDS]
    __attribute__((aligned(64)));
// Deliberately shared and contended.
static volatile unsigned long shared[SHARED_WORDS] __attribute__((aligned(64)));
static volatile unsigned long results[4];

// xorshift64*, so the operand stream is deterministic but not learnable by a
// branch predictor.
static inline unsigned long nextrand(unsigned long *s) {
  unsigned long x = *s;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *s = x;
  return x * 2685821657736338717UL;
}

void __attribute__((noinline)) stress(int cid, int nc) {
  unsigned long rng = 0x9E3779B97F4A7C15UL ^ ((unsigned long)cid * 0x1000193UL);
  unsigned long acc = (unsigned long)cid + 1;
  unsigned long div = 1;

  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "div  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][230] = acc;
    acc += scratch[cid][241];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoand.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[6])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][52] = acc;
    acc += scratch[cid][162];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long v, ok;
    asm volatile(
        "1:\n\t"
        "lr.d  %[v], (%[a])\n\t"
        "addi  %[v], %[v], 1\n\t"
        "sc.d  %[o], %[v], (%[a])\n\t"
        "bnez  %[o], 1b\n\t"
        : [v] "=&r"(v), [o] "=&r"(ok)
        : [a] "r"(&shared[0])
        : "memory");
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][113] = acc;
    acc += scratch[cid][224];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][119] = acc;
    acc += scratch[cid][176];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "rem  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remuw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoxor.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[3])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  asm volatile("fence.i" ::: "memory");
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoor.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[9])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divw  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][55] = acc;
    acc += scratch[cid][83];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][201] = acc;
    acc += scratch[cid][189];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][15] = acc;
    acc += scratch[cid][240];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divuw  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "rem  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][118] = acc;
    acc += scratch[cid][207];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][176] = acc;
    acc += scratch[cid][180];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoswap.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[0])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][105] = acc;
    acc += scratch[cid][218];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][211] = acc;
    acc += scratch[cid][248];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 3\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][169] = acc;
    acc += scratch[cid][234];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][14] = acc;
    acc += scratch[cid][117];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoor.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[5])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divuw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][94] = acc;
    acc += scratch[cid][176];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 3\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divuw  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][107] = acc;
    acc += scratch[cid][221];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "rem  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long v, ok;
    asm volatile(
        "1:\n\t"
        "lr.d  %[v], (%[a])\n\t"
        "addi  %[v], %[v], 1\n\t"
        "sc.d  %[o], %[v], (%[a])\n\t"
        "bnez  %[o], 1b\n\t"
        : [v] "=&r"(v), [o] "=&r"(ok)
        : [a] "r"(&shared[13])
        : "memory");
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][112] = acc;
    acc += scratch[cid][230];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoand.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[1])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  asm volatile("fence.i" ::: "memory");
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][129] = acc;
    acc += scratch[cid][66];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoand.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[6])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][53] = acc;
    acc += scratch[cid][199];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remu  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][69] = acc;
    acc += scratch[cid][173];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][176] = acc;
    acc += scratch[cid][248];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][120] = acc;
    acc += scratch[cid][33];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long v, ok;
    asm volatile(
        "1:\n\t"
        "lr.d  %[v], (%[a])\n\t"
        "addi  %[v], %[v], 1\n\t"
        "sc.d  %[o], %[v], (%[a])\n\t"
        "bnez  %[o], 1b\n\t"
        : [v] "=&r"(v), [o] "=&r"(ok)
        : [a] "r"(&shared[1])
        : "memory");
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remu  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][109] = acc;
    acc += scratch[cid][137];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 3\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][69] = acc;
    acc += scratch[cid][53];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remuw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][41] = acc;
    acc += scratch[cid][136];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 3\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divuw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remuw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "rem  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "div  %[q], %[s], %[d]\n\t"
        "remw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][150] = acc;
    acc += scratch[cid][129];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long v, ok;
    asm volatile(
        "1:\n\t"
        "lr.d  %[v], (%[a])\n\t"
        "addi  %[v], %[v], 1\n\t"
        "sc.d  %[o], %[v], (%[a])\n\t"
        "bnez  %[o], 1b\n\t"
        : [v] "=&r"(v), [o] "=&r"(ok)
        : [a] "r"(&shared[15])
        : "memory");
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, v = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "ld    %[v], 0(%[mm])\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [v] "+&r"(v)
        : [s] "r"(acc), [mm] "r"((unsigned long *)0x0200BFF8));
    acc ^= (v >> 3);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 3\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 7\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 7\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long v, ok;
    asm volatile(
        "1:\n\t"
        "lr.d  %[v], (%[a])\n\t"
        "addi  %[v], %[v], 1\n\t"
        "sc.d  %[o], %[v], (%[a])\n\t"
        "bnez  %[o], 1b\n\t"
        : [v] "=&r"(v), [o] "=&r"(ok)
        : [a] "r"(&shared[15])
        : "memory");
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long old;
    asm volatile("amoxor.d %[o], %[v], (%[a])"
                 : [o] "=r"(old)
                 : [v] "r"(acc | 1UL), [a] "r"(&shared[8])
                 : "memory");
    acc += old;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "rem  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 3\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 1\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 3\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "remuw  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 7\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t1, t2, t3, v = 0;
    asm volatile(
        "and   %[t1], %[s], 1\n\t"
        "beq   %[t1], zero, 1f\n\t"
        "and   %[t2], %[s], 3\n\t"
        "beq   %[t2], zero, 2f\n\t"
        "and   %[t3], %[s], 1\n\t"
        "beq   %[t3], zero, 3f\n\t"
        "divu  %[v], %[s], %[d]\n\t"
        "3:\n\t"
        "addi  %[v], %[v], 1\n\t"
        "2:\n\t"
        "addi  %[v], %[v], 2\n\t"
        "1:\n\t"
        : [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [v] "+&r"(v)
        : [s] "r"(acc), [d] "r"(div));
    acc += v;
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][47] = acc;
    acc += scratch[cid][125];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 15\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "remu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 1\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divuw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 7\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divu  %[q], %[s], %[d]\n\t"
        "divw  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    scratch[cid][240] = acc;
    acc += scratch[cid][78];
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);
  {
    unsigned long t, q = 0, p = 0;
    asm volatile(
        "and   %[t], %[s], 3\n\t"
        "beq   %[t], zero, 1f\n\t"
        "divw  %[q], %[s], %[d]\n\t"
        "divu  %[p], %[q], %[d]\n\t"
        "1:\n\t"
        : [t] "=&r"(t), [q] "+&r"(q), [p] "+&r"(p)
        : [s] "r"(acc), [d] "r"(div));
    acc += q ^ (p << 1);
  }
  div = (nextrand(&rng) & 0xffff) ;
  acc ^= nextrand(&rng);

  results[cid] = acc;
}

// crt.S enters here with cid in a0 and nc in a1 (NUM_CORES is baked into crt.S
// at build time), matching every other benchmark in this tree.
void thread_entry(int cid, int nc) {
  if (cid >= nc) {
    while (1)
      ;
  }

  // Start together so the AMO/LR-SC blocks actually contend rather than each
  // hart running its section alone.
  barrier(nc);
  stress(cid, nc);
  barrier(nc);

  if (cid != 0)
    exit(2);

  // There is no reference value to compare against: the oracle is external
  // (lock-step vs the emulator, or the invariant assertions). Reaching here
  // with every hart through both barriers is the completion condition; the
  // accumulators are printed only so a divergence is visible by eye in a log.
  uart_send_string("mt-stress: acc=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)(results[i] & 0x7fffffff));
    uart_send_string(i == 3 ? "\n" : ",");
  }
  exit(0);
}
