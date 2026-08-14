//**************************************************************************
// mt-ipimux: Linux 6.3 ipi_mux + CLINT MSIP protocol, contended, bare-metal
//--------------------------------------------------------------------------
// Linux SMP on the RTL boots to /init then CPU0 spins forever in
// smp_call_function_many_cond's csd_lock_wait (pc 0x802952cc) while the
// other CPUs idle: a cross-CPU function call was posted but never ran.
// The same image boots to login: on the golden emulator, and the plain
// MSIP->trap path passes bare-metal (mt-ipi), so the suspect is the kernel's
// LAYERED protocol under load, any single stale result of which wedges the
// boot permanently:
//
//   sender:   csd->flag = 1                        (plain store)
//             old = amoor.w.rl (target->bits, BIT) (fetch_or_release)
//             if (!(old & BIT)) write msip[target] (posted MMIO store)
//             spin until csd->flag == 0            (cached load loop)
//   receiver: (trap)  msip[me] = 0
//             ipis = amoand.w.aqrl (me->bits, ~en) (fetch_andnot)
//             for each bit: run callback -> fence rw,w; csd->flag = 0
//
// Failure modes this catches that mt-ipi cannot:
//  - amoor returns stale "bit already set"  -> msip never written again
//  - amoand returns stale 0                 -> vIPI dropped, csd never runs
//  - csd flag release-store never observed  -> sender spins forever
//  - msip posted-write lost under load      -> receiver never traps
//
// All four harts are senders AND receivers simultaneously (maximum
// contention on the bits words and csd lines). Two vIPI types are mixed so
// multi-bit dispatch is exercised. Round-robin targets. Timeouts dump all
// protocol state instead of hanging.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define ROUNDS 3000
#define SPIN_TIMEOUT 40000000UL
#define EN_MASK 0x3u          // two vIPI types enabled, like the kernel's
#define BIT_CALL 0x1u         // IPI_CALL_FUNC: has a csd completion flag
#define BIT_RESCHED 0x2u      // IPI_RESCHEDULE: counted only

// Per-target pending-bits word, one per hart, each on its own cache line
// (the kernel's is percpu). AMO'd cross-hart exactly like ipi_mux.
static volatile unsigned int bits[4][16] __attribute__((aligned(64)));
// csd completion flags: csd[target][sender], cleared by the target's handler
static volatile unsigned int csd[4][4] __attribute__((aligned(64)));
// receive counters per hart per type
volatile unsigned long rx_call[4], rx_resched[4];
volatile unsigned long spurious[4];   // traps with no pending bits

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

static inline void send_msip(int hart)
{
  *(volatile unsigned int *)(0x02000000UL + 4UL * (unsigned)hart) = 1;
}
static inline void clear_own_msip(void)
{
  volatile unsigned int *p =
      (volatile unsigned int *)(0x02000000UL + 4UL * (unsigned)hartid());
  *p = 0;
  (void)*p;  // serialize: level line low before mret
}

// C body of the software-interrupt handler — mirrors clint_ipi_interrupt +
// ipi_mux_process.
void ipimux_isr(void)
{
  int me = hartid();
  clear_own_msip();
  unsigned int ipis = amo_and_aqrl(&bits[me][0], ~EN_MASK) & EN_MASK;
  if (!ipis)
    spurious[me]++;
  if (ipis & BIT_CALL) {
    rx_call[me]++;
    // run the "csd callbacks": complete every sender's pending csd
    for (int s = 0; s < 4; s++)
      if (csd[me][s]) {
        __asm__ volatile("fence rw, w" ::: "memory");  // csd_unlock release
        csd[me][s] = 0;
      }
  }
  if (ipis & BIT_RESCHED)
    rx_resched[me]++;
}

// mtvec stub: save caller-saved regs, call ipimux_isr, mret.
__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global ipimux_trap\n"
    "ipimux_trap:\n"
    "  addi sp, sp, -136\n"
    "  sd ra, 0(sp)\n"
    "  sd t0, 8(sp)\n  sd t1, 16(sp)\n  sd t2, 24(sp)\n"
    "  sd t3, 32(sp)\n  sd t4, 40(sp)\n  sd t5, 48(sp)\n  sd t6, 56(sp)\n"
    "  sd a0, 64(sp)\n  sd a1, 72(sp)\n  sd a2, 80(sp)\n  sd a3, 88(sp)\n"
    "  sd a4, 96(sp)\n  sd a5, 104(sp)\n sd a6, 112(sp)\n sd a7, 120(sp)\n"
    "  call ipimux_isr\n"
    "  ld ra, 0(sp)\n"
    "  ld t0, 8(sp)\n  ld t1, 16(sp)\n  ld t2, 24(sp)\n"
    "  ld t3, 32(sp)\n  ld t4, 40(sp)\n  ld t5, 48(sp)\n  ld t6, 56(sp)\n"
    "  ld a0, 64(sp)\n  ld a1, 72(sp)\n  ld a2, 80(sp)\n  ld a3, 88(sp)\n"
    "  ld a4, 96(sp)\n  ld a5, 104(sp)\n ld a6, 112(sp)\n ld a7, 120(sp)\n"
    "  addi sp, sp, 136\n"
    "  mret\n"
    ".popsection\n");

static inline void ipi_arm(void)
{
  __asm__ volatile(
      "la t0, ipimux_trap\n\t"
      "csrw mtvec, t0\n\t"
      "csrsi mie, 8\n\t"      // mie.MSIE
      "csrsi mstatus, 8\n\t"  // mstatus.MIE
      ::: "t0", "memory");
}

static void fail(const char *what, int me, int target, unsigned long round)
{
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  uart_send_string("mt-ipimux: FAIL ");
  uart_send_string(what);
  uart_send_string(" me=");
  uart_send_integer(me);
  uart_send_string(" tgt=");
  uart_send_integer(target);
  uart_send_string(" round=");
  uart_send_integer((int)round);
  uart_send_string("\nbits=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)bits[i][0]);
    uart_send_string(i == 3 ? " rx_call=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rx_call[i]);
    uart_send_string(i == 3 ? " csd_tgt=" : ",");
  }
  for (int s = 0; s < 4; s++) {
    uart_send_integer((int)csd[target][s]);
    uart_send_string(s == 3 ? "\n" : ",");
  }
  exit(1);
}

// ipi_mux_send_mask + csd wait, one target — the sender half of the kernel
// protocol, verbatim.
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
  // fire-and-forget, like the kernel's
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
  ipi_arm();
  barrier(nc);

  // Every hart: CALL to each other hart (with completion wait) plus a
  // RESCHED to a rotating hart, every round. Maximum cross-traffic.
  for (unsigned long r = 0; r < ROUNDS; r++) {
    for (int off = 1; off < nc; off++)
      send_call(cid, (cid + off) % nc, r);
    send_resched(cid, (cid + 1 + (int)(r % (unsigned long)(nc - 1))) % nc);
  }

  barrier(nc);
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  barrier(nc);

  if (cid != 0)
    exit(2);

  // Reaching here means every csd completion arrived (no send_call timed
  // out) — that IS the pass criterion. Counts are merged-lossy by design
  // (one trap can service several senders), so just require activity.
  int ok = 1;
  for (int h = 0; h < nc; h++) {
    ok &= rx_call[h] > 0;
    ok &= rx_resched[h] > 0;
  }

  uart_send_string("mt-ipimux: rx_call=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rx_call[i]);
    uart_send_string(i == 3 ? " rx_resched=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)rx_resched[i]);
    uart_send_string(i == 3 ? " spurious=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)spurious[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  if (ok) {
    uart_send_string("mt-ipimux: PASS\n");
    exit(0);
  }
  uart_send_string("mt-ipimux: FAIL counts\n");
  exit(1);
}
