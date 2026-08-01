//**************************************************************************
// mt-lrscirq: cmpxchg contention under continuous timer interrupts
//--------------------------------------------------------------------------
// The reservation-guard model passes mt-lrsc (no IRQs) but the Linux boot
// freezes core0 at resume_kernel+0x4 (trap-return after a timer IRQ, the
// tp->preempt_count load never completes) while other harts idle. The
// missing ingredient in mt-lrsc is TRAPS landing inside/around LR/SC
// windows: an interrupt between LR and SC leaves the reservation set with
// no SC (guard runs its tenure, blocked-until-snoop-lands), the handler's
// own memory ops interleave with the guard state, and the trap-return path
// issues fresh loads immediately.
//
// This test = mt-lrsc's mutex-owner cmpxchg handoff (phase 3, the one that
// exposed both guard deadlocks) with every hart re-arming its CLINT timer
// every 200 mtime ticks (~3200 cycles) from a tiny mtvec handler, exactly
// like mt-divirq. Any guard/trap interaction that can strand a hart's
// D-cache pipeline should reproduce here within a few million cycles.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef N_LOCK
#define N_LOCK 4000
#endif
#define SPIN_TIMEOUT 400000000UL

static volatile unsigned long owner __attribute__((aligned(64)));
static volatile unsigned long lock_total __attribute__((aligned(64)));
static volatile unsigned long crit __attribute__((aligned(64)));
volatile unsigned long irq_count;
volatile unsigned long owner_corrupt;

// Timer handler: count, re-arm mtimecmp[mhartid] = mtime + 200, mret.
__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global lrscirq_handler\n"
    "lrscirq_handler:\n"
    "  addi sp, sp, -48\n"
    "  sd t0, 0(sp)\n"
    "  sd t1, 8(sp)\n"
    "  sd t2, 16(sp)\n"
    "  sd t3, 24(sp)\n"
    "  la t0, irq_count\n"
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

static inline void irq_arm(void)
{
  __asm__ volatile(
      "la t0, lrscirq_handler\n\t"
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

static inline unsigned long cmpxchg64(volatile unsigned long *p,
                                      unsigned long old, unsigned long nw)
{
  unsigned long v, fail;
  __asm__ volatile(
      "1: lr.d.aq %0, (%2)\n\t"
      "   bne %0, %3, 2f\n\t"
      "   sc.d.rl %1, %4, (%2)\n\t"
      "   bnez %1, 1b\n\t"
      "2:"
      : "=&r"(v), "=&r"(fail)
      : "r"(p), "r"(old), "r"(nw)
      : "memory");
  return v;
}

static void fail(const char *what, int me, unsigned long round)
{
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  uart_send_string("mt-lrscirq: FAIL ");
  uart_send_string(what);
  uart_send_string(" me=");
  uart_send_integer(me);
  uart_send_string(" round=");
  uart_send_integer((int)round);
  uart_send_string(" owner=");
  uart_send_integer((int)owner);
  uart_send_string(" total=");
  uart_send_integer((int)lock_total);
  uart_send_string("\n");
  exit(1);
}

void thread_entry(int cid, int nc)
{
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  barrier(nc);
  irq_arm();
  barrier(nc);

  for (unsigned long i = 0; i < N_LOCK; i++) {
    unsigned long spins = 0;
    unsigned long self = (unsigned long)(cid + 1) << 8;
    while (cmpxchg64(&owner, 0, self) != 0)
      if (++spins > SPIN_TIMEOUT)
        fail("acquire-timeout", cid, i);
    if (owner != self)
      owner_corrupt++;
    crit++;
    lock_total++;
    __asm__ volatile("fence rw, w" ::: "memory");
    owner = 0;
  }

  barrier(nc);
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  barrier(nc);

  if (cid != 0)
    exit(2);

  uart_send_string("mt-lrscirq: total=");
  uart_send_integer((int)lock_total);
  uart_send_string(" crit=");
  uart_send_integer((int)crit);
  uart_send_string(" irqs=");
  uart_send_integer((int)irq_count);
  uart_send_string(" corrupt=");
  uart_send_integer((int)owner_corrupt);
  uart_send_string("\n");

  if (lock_total == (unsigned long)nc * N_LOCK &&
      crit == (unsigned long)nc * N_LOCK &&
      owner_corrupt == 0 && irq_count > 100) {
    uart_send_string("mt-lrscirq: PASS\n");
    exit(0);
  }
  uart_send_string("mt-lrscirq: FAIL totals\n");
  exit(1);
}
