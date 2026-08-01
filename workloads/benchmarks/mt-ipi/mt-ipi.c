//**************************************************************************
// mt-ipi: CLINT MSIP software-interrupt (IPI) delivery, hart -> hart
//--------------------------------------------------------------------------
// Linux SMP boots to /init on the RTL, then CPU0 wedges forever in
// smp_call_function_many_cond's csd_lock_wait while CPUs 1-3 cycle through
// the idle loop: a cross-CPU function-call IPI (CLINT msip write) is sent
// but the target harts never take the software interrupt. Secondary bring-up
// on this kernel is spin-wait based, so that csd call is the FIRST real use
// of msip IPIs on the RTL — nothing has ever exercised the
// msipShared -> core.MSIP -> mip.MSIP -> trap path under load.
//
// This test isolates exactly that path, bare-metal:
//   phase 1: hart0 sends ROUNDS sequential IPIs to each of harts 1..3 and
//            waits for the per-hart receive counter to advance each time.
//   phase 2: each secondary in turn (handshake via `go`) sends one IPI back
//            to hart0.
// Every hart runs with mie.MSIE + mstatus.MIE set and a tiny mtvec handler
// that clears its own msip (with a serializing read-back so the level-
// triggered line is low before mret), bumps ipi_count[mhartid], and mrets.
// Timeouts print FAIL with the observed counters instead of hanging.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define ROUNDS 8
#define SPIN_TIMEOUT 20000000UL

volatile unsigned long ipi_count[4];
volatile int go = 0;

// M-mode software-interrupt handler: clear own msip, read it back (the line
// is level-triggered off msipShared — mret with it still high would re-trap
// and double-count), bump ipi_count[mhartid], mret.
__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global ipi_handler\n"
    "ipi_handler:\n"
    "  addi sp, sp, -32\n"
    "  sd t0, 0(sp)\n"
    "  sd t1, 8(sp)\n"
    "  sd t2, 16(sp)\n"
    "  csrr t0, mhartid\n"
    "  slli t1, t0, 2\n"
    "  li t2, 0x02000000\n"
    "  add t2, t2, t1\n"
    "  sw zero, 0(t2)\n"
    "  lw t1, 0(t2)\n"        // serialize: msip write reached the CLINT
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
      "la t0, ipi_handler\n\t"
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

static void fail(const char *what, int hart)
{
  uart_send_string("mt-ipi: FAIL ");
  uart_send_string(what);
  uart_send_string(" hart=");
  uart_send_integer(hart);
  uart_send_string(" counts=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)ipi_count[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  exit(1);
}

// Wait until *ctr >= want; FAIL on timeout.
static void wait_count(volatile unsigned long *ctr, unsigned long want,
                       const char *what, int hart)
{
  for (unsigned long spin = 0; *ctr < want; spin++)
    if (spin > SPIN_TIMEOUT)
      fail(what, hart);
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
    // Phase 1: sequential IPIs to each secondary, verified per round.
    for (unsigned long r = 1; r <= ROUNDS; r++)
      for (int t = 1; t < nc; t++) {
        send_ipi(t);
        wait_count(&ipi_count[t], r, "no-ipi-rx", t);
      }
    // Phase 2: each secondary sends one IPI back to hart0.
    for (int t = 1; t < nc; t++) {
      go = t;
      wait_count(&ipi_count[0], (unsigned long)t, "no-ipi-to-h0", t);
    }
  } else {
    // Secondaries: idle-spin (like the kernel's do_idle) while receiving,
    // then send one IPI to hart0 when told.
    wait_count(&ipi_count[cid], ROUNDS, "rx-self-wait", cid);
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

  uart_send_string("mt-ipi: counts=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)ipi_count[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  if (ok) {
    uart_send_string("mt-ipi: PASS\n");
    exit(0);
  }
  uart_send_string("mt-ipi: FAIL counts\n");
  exit(1);
}
