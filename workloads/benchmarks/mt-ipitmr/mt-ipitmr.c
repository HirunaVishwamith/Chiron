//**************************************************************************
// mt-ipitmr: ipi_mux + CLINT MSIP cross-calls CONCURRENT with CLINT timer
// interrupts — the one thing mt-ipimux does not model.
//--------------------------------------------------------------------------
// Linux SMP on the RTL boots to /init then a CPU spins forever in
// smp_call_function_many_cond's csd_lock_wait while the other CPUs sit in the
// idle loop. mt-ipimux replicates the ipi_mux/msip protocol under maximum
// cross-hart contention and PASSES — so the plain layered IPI protocol is not
// the fault on its own. The one property of the real idle harts that neither
// mt-ipi nor mt-ipimux reproduces: the target harts are taking *periodic timer
// interrupts* (MTIP) at the same time the software cross-call IPI (MSIP)
// arrives. chiron's interrupt-injection FSM (core.scala) latches exactly one
// interrupt kind at a time and drains the pipeline with a synthetic system
// instruction; a timer trap racing a software trap can drop one of them.
//
// This test adds exactly that: every hart arms a free-running periodic timer
// interrupt (rearmed inside its handler) AND runs the full mt-ipimux cross-
// call/vIPI traffic. A single dropped MSIP (target never traps) or dropped
// csd release (sender spins) makes send_call time out -> FAIL with the full
// protocol + timer state, reproducing the Linux csd_lock_wait wedge bare-metal
// in millions (not billions) of cycles.
//
// Phases per hart are phase-drifted (different timer deltas) so the timer
// edge sweeps through all points of the IPI protocol across rounds.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define ROUNDS 4000
#define SPIN_TIMEOUT 2000000UL  /* generous: legit csd release lands well within
                                   this even under timer+IPI contention; a real
                                   dropped-IPI/lost-write wedge trips it -> FAIL */
#define EN_MASK 0x3u          // two vIPI types, like the kernel's
#define BIT_CALL 0x1u         // IPI_CALL_FUNC: has a csd completion flag
#define BIT_RESCHED 0x2u      // IPI_RESCHEDULE: counted only

#define CLINT_MSIP     0x02000000UL
#define CLINT_MTIMECMP 0x02004000UL
#define CLINT_MTIME    0x0200bff8UL

// Per-hart timer period in mtime ticks; drifted per hart so the timer edge
// sweeps across the IPI critical section over successive rounds.
static const unsigned long TIMER_DELTA[4] = { 211, 277, 349, 421 };

static volatile unsigned int bits[4][16] __attribute__((aligned(64)));
static volatile unsigned int csd[4][4]   __attribute__((aligned(64)));
volatile unsigned long rx_call[4], rx_resched[4];
volatile unsigned long spurious[4];   // software traps with no pending bits
volatile unsigned long timer_irqs[4]; // timer traps taken per hart

static inline unsigned int amo_or_rl(volatile unsigned int *p, unsigned int v)
{
  unsigned int old;
  __asm__ volatile("amoor.w.rl %0, %2, (%1)"
                   : "=r"(old) : "r"(p), "r"(v) : "memory");
  return old;
}
static inline unsigned int amo_and_aqrl(volatile unsigned int *p, unsigned int v)
{
  unsigned int old;
  __asm__ volatile("amoand.w.aqrl %0, %2, (%1)"
                   : "=r"(old) : "r"(p), "r"(v) : "memory");
  return old;
}
static inline int hartid(void)
{
  int h;
  __asm__ volatile("csrr %0, mhartid" : "=r"(h));
  return h;
}
static inline unsigned long read_mtime(void)
{
  return *(volatile unsigned long *)CLINT_MTIME;
}
static inline void set_mtimecmp(int hart, unsigned long v)
{
  *(volatile unsigned long *)(CLINT_MTIMECMP + 8UL * (unsigned)hart) = v;
}
static inline void send_msip(int hart)
{
  *(volatile unsigned int *)(CLINT_MSIP + 4UL * (unsigned)hart) = 1;
}
static inline void clear_own_msip(void)
{
  volatile unsigned int *p =
      (volatile unsigned int *)(CLINT_MSIP + 4UL * (unsigned)hartid());
  *p = 0;
  (void)*p;  // serialize: level line low before mret
}

// software-interrupt body — mirrors clint_ipi_interrupt + ipi_mux_process.
static void soft_isr(int me)
{
  clear_own_msip();
  unsigned int ipis = amo_and_aqrl(&bits[me][0], ~EN_MASK) & EN_MASK;
  if (!ipis)
    spurious[me]++;
  if (ipis & BIT_CALL) {
    rx_call[me]++;
    for (int s = 0; s < 4; s++)
      if (csd[me][s]) {
        __asm__ volatile("fence rw, w" ::: "memory");  // csd_unlock release
        csd[me][s] = 0;
      }
  }
  if (ipis & BIT_RESCHED)
    rx_resched[me]++;
}

// timer-interrupt body — rearm the compare (clears MTIP) and count.
static void timer_isr(int me)
{
  set_mtimecmp(me, read_mtime() + TIMER_DELTA[me]);
  timer_irqs[me]++;
}

// Combined trap handler: dispatch on mcause. Interrupt bit is msb.
// cause 3 = machine software (MSIP), cause 7 = machine timer (MTIP).
void trap_isr(void)
{
  unsigned long cause;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  int me = hartid();
  unsigned long code = cause & 0xffUL;
  if (cause >> 63) {
    if (code == 3)
      soft_isr(me);
    else if (code == 7)
      timer_isr(me);
  }
}

__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global ipitmr_trap\n"
    "ipitmr_trap:\n"
    "  addi sp, sp, -136\n"
    "  sd ra, 0(sp)\n"
    "  sd t0, 8(sp)\n  sd t1, 16(sp)\n  sd t2, 24(sp)\n"
    "  sd t3, 32(sp)\n  sd t4, 40(sp)\n  sd t5, 48(sp)\n  sd t6, 56(sp)\n"
    "  sd a0, 64(sp)\n  sd a1, 72(sp)\n  sd a2, 80(sp)\n  sd a3, 88(sp)\n"
    "  sd a4, 96(sp)\n  sd a5, 104(sp)\n sd a6, 112(sp)\n sd a7, 120(sp)\n"
    "  call trap_isr\n"
    "  ld ra, 0(sp)\n"
    "  ld t0, 8(sp)\n  ld t1, 16(sp)\n  ld t2, 24(sp)\n"
    "  ld t3, 32(sp)\n  ld t4, 40(sp)\n  ld t5, 48(sp)\n  ld t6, 56(sp)\n"
    "  ld a0, 64(sp)\n  ld a1, 72(sp)\n  ld a2, 80(sp)\n  ld a3, 88(sp)\n"
    "  ld a4, 96(sp)\n  ld a5, 104(sp)\n ld a6, 112(sp)\n ld a7, 120(sp)\n"
    "  addi sp, sp, 136\n"
    "  mret\n"
    ".popsection\n");

static inline void irq_arm(void)
{
  int me = hartid();
  set_mtimecmp(me, read_mtime() + TIMER_DELTA[me]);   // arm first timer edge
  __asm__ volatile(
      "la t0, ipitmr_trap\n\t"
      "csrw mtvec, t0\n\t"
      "li t0, 0x88\n\t"        // mie.MSIE (8) | mie.MTIE (0x80)
      "csrs mie, t0\n\t"
      "csrsi mstatus, 8\n\t"   // mstatus.MIE
      ::: "t0", "memory");
}

static void fail(const char *what, int me, int target, unsigned long round)
{
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  uart_send_string("mt-ipitmr: FAIL ");
  uart_send_string(what);
  uart_send_string(" me=");   uart_send_integer(me);
  uart_send_string(" tgt=");  uart_send_integer(target);
  uart_send_string(" round="); uart_send_integer((int)round);
  uart_send_string("\nbits=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)bits[i][0]);
    uart_send_string(i == 3 ? " rx_call=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rx_call[i]);
    uart_send_string(i == 3 ? " timer_irqs=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)timer_irqs[i]);
    uart_send_string(i == 3 ? " csd_tgt=" : ",");
  }
  for (int s = 0; s < 4; s++) {
    uart_send_integer((int)csd[target][s]);
    uart_send_string(s == 3 ? "\n" : ",");
  }
  exit(1);
}

static void send_call(int me, int target, unsigned long round)
{
  csd[target][me] = 1;                       // csd_lock + queue the callback
  unsigned int pending = amo_or_rl(&bits[target][0], BIT_CALL);
  if (!(pending & BIT_CALL))
    send_msip(target);
  for (unsigned long spin = 0; csd[target][me] != 0; spin++)
    if (spin > SPIN_TIMEOUT)
      fail("csd-stuck", me, target, round);
}

static void send_resched(int me, int target)
{
  unsigned int pending = amo_or_rl(&bits[target][0], BIT_RESCHED);
  if (!(pending & BIT_RESCHED))
    send_msip(target);
  (void)me;
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

  // DIAGNOSTIC: divide-free target arithmetic (no remu/remw) to test whether
  // the wedge needs a divide at the interrupt-injection commit head.
  unsigned rmod = 0;  // == r % (nc-1), maintained without a divide
  for (unsigned long r = 0; r < ROUNDS; r++) {
    for (int off = 1; off < nc; off++) {
      int t = cid + off; while (t >= nc) t -= nc;   // (cid+off) % nc
      send_call(cid, t, r);
    }
    int rt = cid + 1 + (int)rmod; while (rt >= nc) rt -= nc;
    send_resched(cid, rt);
    if (++rmod >= (unsigned)(nc - 1)) rmod = 0;
  }

  barrier(nc);
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  barrier(nc);

  if (cid != 0)
    exit(2);

  int ok = 1;
  for (int h = 0; h < nc; h++) {
    ok &= rx_call[h] > 0;
    ok &= rx_resched[h] > 0;
    ok &= timer_irqs[h] > 0;   // timers must actually have fired
  }

  uart_send_string("mt-ipitmr: rx_call=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rx_call[i]);
    uart_send_string(i == 3 ? " rx_resched=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rx_resched[i]);
    uart_send_string(i == 3 ? " timer_irqs=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)timer_irqs[i]);
    uart_send_string(i == 3 ? " spurious=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)spurious[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  if (ok) {
    uart_send_string("mt-ipitmr: PASS\n");
    exit(0);
  }
  uart_send_string("mt-ipitmr: FAIL counts\n");
  exit(1);
}
