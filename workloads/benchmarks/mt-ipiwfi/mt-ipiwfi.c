//**************************************************************************
// mt-ipiwfi: IPI delivery to a hart idling on WFI
//--------------------------------------------------------------------------
// mt-ipi already proves the CLINT msip -> mip.MSIP -> trap path works, and it
// PASSES. Yet quad-core Linux still hangs after "Run /init as init process":
// at 1.589 billion cycles hart1 sits in smp_call_function_many_cond+0x2e4
// (csd_lock_wait, waiting for a cross-call to be serviced) while harts 0/2/3
// cycle through do_idle / tick_nohz_idle_stop_tick / get_next_timer_interrupt
// and never take the IPI. Nothing is wedged — the ROB is fine, every hart
// advances — so this is a lost wakeup, not a pipeline stall.
//
// The one thing Linux's idle loop does that mt-ipi's receivers do not is
// execute WFI. And WFI is not implemented in this RTL at all: Decode's
// system-instruction decode (decode.scala ~line 756) handles exactly three
// funct12 values — 0x302 (mret), 0x000 (ecall) and 0x800 (the injected
// interrupt pseudo-instruction) — so funct12=0x105 falls through every branch
// and retires as a NOP. Retiring WFI as a NOP is architecturally legal, but it
// is still a SYSTEM instruction, and system instructions in this core suppress
// interrupt delivery two ways:
//   * decode.scala:912  `when(stallReg) { canTakeInterrupt := false }` and the
//     same for canTakeSoftInterrupt — no interrupt while a system instruction
//     is in the pipeline;
//   * core.scala:1136/1155 `lastRetiredSystem` blocks the inject FSM from
//     leaving waitForMTIP when the last retired instruction was a SYSTEM one.
// An idle loop that executes WFI every iteration therefore keeps at least one
// of those gates asserted essentially all the time — the hart spins forever and
// the IPI is never delivered. That is a livelock, and it is invisible to every
// existing benchmark because NOT ONE of them executes a WFI.
//
// This test is mt-ipi with a single change: the receivers idle on WFI instead
// of a plain spin. Build it both ways to get a clean A/B:
//   -DIDLE_WFI=1  (default) receivers execute `wfi` in the idle loop
//   -DIDLE_WFI=0            receivers execute `nop` — the mt-ipi control
// If the WFI build FAILs with no-ipi-rx and the nop build PASSes, the idle-loop
// interrupt gating is confirmed as the Linux hang, isolated in ~100k cycles
// instead of 1.6 billion.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef IDLE_WFI
#define IDLE_WFI 1
#endif

#define ROUNDS 8
// Deliberately short: a working IPI arrives in hundreds of cycles, so a hang
// should be reported fast rather than eating the whole simulation budget.
#define SPIN_TIMEOUT 2000000UL

volatile unsigned long ipi_count[4];
volatile int go = 0;

// Identical to mt-ipi's handler: clear own msip, read it back so the
// level-triggered line is low before mret, bump the counter, return.
__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global ipiwfi_handler\n"
    "ipiwfi_handler:\n"
    "  addi sp, sp, -32\n"
    "  sd t0, 0(sp)\n"
    "  sd t1, 8(sp)\n"
    "  sd t2, 16(sp)\n"
    "  csrr t0, mhartid\n"
    "  slli t1, t0, 2\n"
    "  li t2, 0x02000000\n"
    "  add t2, t2, t1\n"
    "  sw zero, 0(t2)\n"
    "  lw t1, 0(t2)\n"
    "  la t1, ipi_count\n"
    "  slli t0, t0, 3\n"
    "  add t1, t1, t0\n"
    "  ld t0, 0(t1)\n"
    "  addi t0, t0, 1\n"
    "  sd t0, 0(t1)\n"
    "  ld t0, 0(sp)\n"
    "  ld t1, 8(sp)\n"
    "  ld t2, 16(sp)\n"
    "  addi sp, sp, 32\n"
    "  mret\n"
    ".popsection\n");

static inline void ipi_arm(void)
{
  __asm__ volatile(
      "la t0, ipiwfi_handler\n\t"
      "csrw mtvec, t0\n\t"
      "csrsi mie, 8\n\t"      // mie.MSIE
      "csrsi mstatus, 8\n\t"  // mstatus.MIE
      ::: "t0", "memory");
}

static inline void send_ipi(int hart)
{
  volatile unsigned int *msip =
      (volatile unsigned int *)(0x02000000UL + 4UL * (unsigned)hart);
  *msip = 1;
}

// The idle instruction under test. This is the ONLY difference from mt-ipi.
static inline void idle_instruction(void)
{
#if IDLE_WFI
  __asm__ volatile("wfi" ::: "memory");
#else
  __asm__ volatile("nop" ::: "memory");
#endif
}

static void fail(const char *what, int hart)
{
  uart_send_string("mt-ipiwfi: FAIL ");
  uart_send_string(what);
  uart_send_string(" hart=");
  uart_send_integer(hart);
  uart_send_string(" idle=");
  uart_send_string(IDLE_WFI ? "wfi" : "nop");
  uart_send_string(" counts=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)ipi_count[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  exit(1);
}

// Sender-side wait: a plain spin (hart0 is the one doing the waiting, exactly
// like csd_lock_wait). Only the RECEIVERS idle on the instruction under test.
static void wait_count(volatile unsigned long *ctr, unsigned long want,
                       const char *what, int hart)
{
  for (unsigned long spin = 0; *ctr < want; spin++)
    if (spin > SPIN_TIMEOUT)
      fail(what, hart);
}

// Receiver idle loop, shaped like the kernel's do_idle(): re-check the
// condition, execute the idle instruction, repeat. The interrupt has to be
// taken from inside this loop.
static void idle_until(volatile unsigned long *ctr, unsigned long want,
                       const char *what, int hart)
{
  for (unsigned long spin = 0; *ctr < want; spin++) {
    idle_instruction();
    if (spin > SPIN_TIMEOUT)
      fail(what, hart);
  }
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

  if (cid == 0) {
    // Phase 1: sequential IPIs to each secondary, verified per round. Each
    // target is sitting in the WFI idle loop when the IPI lands.
    for (unsigned long r = 1; r <= ROUNDS; r++)
      for (int t = 1; t < nc; t++) {
        send_ipi(t);
        wait_count(&ipi_count[t], r, "no-ipi-rx", t);
      }
    // Phase 2: each secondary sends one IPI back to hart0, which is itself
    // idling on the instruction under test this time.
    for (int t = 1; t < nc; t++) {
      go = t;
      idle_until(&ipi_count[0], (unsigned long)t, "no-ipi-to-h0", t);
    }
  } else {
    idle_until(&ipi_count[cid], ROUNDS, "rx-self-wait", cid);
    while (go != cid)
      ;
    send_ipi(0);
  }

  barrier(nc);

  if (cid != 0)
    exit(2);

  int ok = ipi_count[0] == (unsigned long)(nc - 1);
  for (int t = 1; t < nc; t++)
    ok &= ipi_count[t] == ROUNDS;

  uart_send_string("mt-ipiwfi: idle=");
  uart_send_string(IDLE_WFI ? "wfi" : "nop");
  uart_send_string(" counts=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)ipi_count[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  if (ok) {
    uart_send_string("mt-ipiwfi: PASS\n");
    exit(0);
  }
  uart_send_string("mt-ipiwfi: FAIL counts\n");
  exit(1);
}
