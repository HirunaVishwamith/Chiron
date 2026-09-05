//**************************************************************************
// mt-litmus: run a RISC-V memory-model litmus test on two harts and report
// the histogram of observed final states.
//--------------------------------------------------------------------------
// This is the RTL side of the memory-model envelope measurement. Three sets
// are being compared, per test:
//
//   allowed   -- what RVWMO permits          (litmus-tests-riscv model-results/herd.logs)
//   silicon   -- what a real chip exhibits   (hw-results/SiFive-Freedom-U540.log)
//   chiron    -- what THIS RTL exhibits      (this program)
//
// The interesting column is the third. Example, test SB: the model allows four
// states including the relaxed 0:x7=0 /\ 1:x7=0, and the U540 -- four in-order
// U54 cores -- never observed it once in 1.2e9 iterations (Positive: 0). An
// out-of-order machine has more ways to expose it. Whether Chiron does is a
// measurement, not an opinion.
//
// Correctness note: an outcome OUTSIDE the model's allowed set is an RVWMO
// violation, i.e. a real bug. An outcome inside it is not -- a machine is
// permitted to be stronger than the model.
//
//   SB  (store buffering)      P0: x=1; r0=y     P1: y=1; r1=x
//                              relaxed outcome r0=0 /\ r1=0
//                              needs BOTH stores to sink past the loads.
//                              Chiron commits stores in order at the ROB head
//                              (no post-commit store buffer), so a negative
//                              result here is itself a finding: the store gate
//                              that costs ~37% of runtime also makes the
//                              machine accidentally stronger than RVWMO.
//
//   LB  (load buffering, ctrl) P0: r0=x; if(r0) {}; y=1
//                              P1: r1=y; x=1
//                              relaxed outcome r0=1 /\ r1=1
//                              P1 has no dependency, so its store may issue
//                              early; this probes the LOAD/speculation side.
//
// Each thread's body is a single asm block so the compiler cannot reorder the
// pair; any reordering observed is the hardware's.
//
// Build:  make litmus-bin              (LITMUS=SB by default)
//         make litmus-bin LITMUS=LB
// Run  :  build/profile_quad_fast.out --image bins/mt-litmus-q4.bin \
//           --name mt-litmus --done-pc <exit> --done-a0 0
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

// 1 = SB (store buffering), 2 = LB (load buffering with a control dependency)
#ifndef LITMUS_SB
#define LITMUS_SB 1
#endif

#ifndef ITERS
#define ITERS 2000
#endif

// The CONTROL. With a full fence between each thread's store and its load,
// RVWMO forbids the relaxed outcome. If the relaxed outcome survives the
// fence, the measurement is not measuring what it claims -- either the harness
// is wrong, or the RTL violates RVWMO. Either way, do not report a number
// until LITMUS_FENCE=1 gives zero.
#ifndef LITMUS_FENCE
#define LITMUS_FENCE 0
#endif
#if LITMUS_FENCE
#define LITMUS_BARRIER "fence rw,rw\n\t"
#else
#define LITMUS_BARRIER ""
#endif

// Separate cache lines: this is about memory ordering, not false sharing.
typedef struct { volatile unsigned long v; unsigned char pad[56]; } cell_t;

static cell_t xv __attribute__((aligned(64)));
static cell_t yv __attribute__((aligned(64)));

// Per-iteration observations, written by the hart that made them and only
// read after the final barrier, so recording never perturbs the test itself.
static volatile unsigned long obs0[ITERS];
static volatile unsigned long obs1[ITERS];

void thread_entry(int cid, int nc)
{
  if (cid >= nc) { while (1) ; }

  initialize_count_asm(0);
  barrier(nc);

  for (unsigned long i = 0; i < ITERS; i++) {
    // Hart 0 clears both locations; the barrier below publishes that to hart 1
    // and starts both threads together.
    if (cid == 0) { xv.v = 0; yv.v = 0; }
    barrier(nc);

    unsigned long r = 0;

#if LITMUS_SB
    if (cid == 0) {
      // P0: sd 1,(x) ; ld r,(y)
      __asm__ __volatile__(
          "sd %[one], 0(%[px])\n\t"
          LITMUS_BARRIER
          "ld %[r],   0(%[py])\n\t"
          : [r] "=&r"(r)
          : [one] "r"(1UL), [px] "r"(&xv.v), [py] "r"(&yv.v)
          : "memory");
      obs0[i] = r;
    } else if (cid == 1) {
      // P1: sd 1,(y) ; ld r,(x)
      __asm__ __volatile__(
          "sd %[one], 0(%[py])\n\t"
          LITMUS_BARRIER
          "ld %[r],   0(%[px])\n\t"
          : [r] "=&r"(r)
          : [one] "r"(1UL), [py] "r"(&yv.v), [px] "r"(&xv.v)
          : "memory");
      obs1[i] = r;
    }
#else
    if (cid == 0) {
      // P0: ld r,(x) ; bne r,zero,1f ; 1: sd 1,(y)   -- control dependency
      __asm__ __volatile__(
          "ld  %[r], 0(%[px])\n\t"
          LITMUS_BARRIER
          "bne %[r], zero, 1f\n\t"
          "1:\n\t"
          "sd  %[one], 0(%[py])\n\t"
          : [r] "=&r"(r)
          : [px] "r"(&xv.v), [py] "r"(&yv.v), [one] "r"(1UL)
          : "memory");
      obs0[i] = r;
    } else if (cid == 1) {
      // P1: ld r,(y) ; sd 1,(x)   -- no dependency
      __asm__ __volatile__(
          "ld %[r],   0(%[py])\n\t"
          LITMUS_BARRIER
          "sd %[one], 0(%[px])\n\t"
          : [r] "=&r"(r)
          : [py] "r"(&yv.v), [px] "r"(&xv.v), [one] "r"(1UL)
          : "memory");
      obs1[i] = r;
    }
#endif

    barrier(nc);
  }

  barrier(nc);

  if (cid != 0)
    exit(2);

  // Histogram of (r0, r1) over the four possible outcomes.
  int h[2][2] = {{0, 0}, {0, 0}};
  int odd = 0;
  for (unsigned long i = 0; i < ITERS; i++) {
    unsigned long a = obs0[i], b = obs1[i];
    if (a > 1 || b > 1) { odd++; continue; }
    h[a][b]++;
  }

#if LITMUS_SB
  uart_send_string("LITMUS SB  P0:{x=1;r0=y}  P1:{y=1;r1=x}\n");
#else
  uart_send_string("LITMUS LB+ctrl  P0:{r0=x;ctrl;y=1}  P1:{r1=y;x=1}\n");
#endif
#if LITMUS_FENCE
  uart_send_string("  [CONTROL: fence rw,rw between the pair -- relaxed outcome must be 0]\n");
#endif
  uart_send_string("iterations "); uart_send_integer(ITERS); uart_send_string("\n");
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 2; b++) {
      uart_send_string("  r0="); uart_send_integer(a);
      uart_send_string(" r1=");  uart_send_integer(b);
      uart_send_string(" : ");   uart_send_integer(h[a][b]);
      uart_send_string("\n");
    }
  if (odd) { uart_send_string("  OUT-OF-RANGE "); uart_send_integer(odd); uart_send_string("\n"); }

  // The relaxed outcome each test is named for.
#if LITMUS_SB
  int relaxed = h[0][0];   // SB: exists (0:x7=0 /\ 1:x7=0)
  uart_send_string("RELAXED(r0=0,r1=0) ");
#else
  int relaxed = h[1][1];   // LB: exists (0:x5=1 /\ 1:x5=1)
  uart_send_string("RELAXED(r0=1,r1=1) ");
#endif
  uart_send_integer(relaxed);
  uart_send_string(relaxed ? "  OBSERVED\n" : "  not observed\n");

  uart_send_string("BENCHMARK COMPLETE\n");
  exit(0);
}
