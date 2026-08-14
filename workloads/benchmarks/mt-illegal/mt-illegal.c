//**************************************************************************
// mt-illegal: does an illegal instruction TRAP instead of wedging the core?
//--------------------------------------------------------------------------
// Regression for the illegal-instruction trap added 2026-08-12.
//
// Before that change chiron had full exception plumbing in the ROB (mcause,
// mtval, exceptionOccurred, flush-on-commit) but nothing ever raised an
// exception -- core.scala hardwired `execPorts.exceptionOccurred := false.B`.
// An illegal instruction is never issued by the scheduler, so no execute port
// ever writes its ready bit, and the ROB head waits forever for a completion
// that cannot come. Worse, an all-zero word has instruction(6,2)===0, which is
// the LOAD encoding, so the commit gate in core.scala also held it waiting on a
// loadCommit that never arrives. The machine wedged SILENTLY and permanently.
//
// That is not hypothetical: on the quad-core Linux boot hart0 jumped to
// 0x81b03840 -- past the end of allocated memory -- fetched 16 words of zeros,
// filled every ROB entry with 0x00000000 and stopped dead at ~1.23e9 cycles
// with all functional units idle. It cost roughly twelve hours of wall clock to
// find, because a hang tells you nothing about where it came from. A trap turns
// that into a kernel oops naming the faulting PC.
//
// This test executes a literal 0x00000000 word -- exactly what the core fetches
// from unpopulated memory -- and requires that:
//   * the trap is taken at all (no hang),
//   * mcause == 2   (illegal instruction),
//   * mtval  == 0   (the offending word),
//   * mepc points at the bad word, so a handler can report/skip it.
//
// A pre-fix RTL does not fail this test, it HANGS on it -- which is the point,
// and why the harness timeout is the failure mode to expect on a regression.
//
// exit(0) pass; exit(1) trap taken but wrong CSRs; a timeout means no trap.
//**************************************************************************

#include "util.h"

#include <stdint.h>

extern void exit(int status);

// Written by the trap handler installed below.
volatile unsigned long trap_seen   = 0;
volatile unsigned long trap_mcause = 0xffffffffUL;
volatile unsigned long trap_mtval  = 0xffffffffUL;
volatile unsigned long trap_mepc   = 0;

// Landing pad for the trap. Records the CSRs, then returns to the instruction
// AFTER the illegal word (mepc + 4) so the test can continue and report.
extern void illegal_trap_entry(void);

__attribute__((naked)) void illegal_trap_entry(void)
{
  __asm__ volatile(
      // Save the two scratch registers we use.
      "addi sp, sp, -32\n"
      "sd   t0, 0(sp)\n"
      "sd   t1, 8(sp)\n"
      // trap_seen = 1
      "la   t0, trap_seen\n"
      "li   t1, 1\n"
      "sd   t1, 0(t0)\n"
      // trap_mcause = mcause
      "csrr t1, mcause\n"
      "la   t0, trap_mcause\n"
      "sd   t1, 0(t0)\n"
      // trap_mtval = mtval
      "csrr t1, mtval\n"
      "la   t0, trap_mtval\n"
      "sd   t1, 0(t0)\n"
      // trap_mepc = mepc
      "csrr t1, mepc\n"
      "la   t0, trap_mepc\n"
      "sd   t1, 0(t0)\n"
      // Resume after the offending word so the test can report a verdict.
      "addi t1, t1, 4\n"
      "csrw mepc, t1\n"
      "ld   t0, 0(sp)\n"
      "ld   t1, 8(sp)\n"
      "addi sp, sp, 32\n"
      "mret\n");
}

void thread_entry(int cid, int nc)
{
  // Single-hart test: the trap path is per-hart and needs no SMP interaction.
  if (cid != 0) {
    while (1)
      ;
  }

  // Install the handler.
  __asm__ volatile("csrw mtvec, %0" ::"r"(&illegal_trap_entry));

  unsigned long before_pc;
  __asm__ volatile("auipc %0, 0" : "=r"(before_pc));

  // The illegal word itself: 0x00000000, the same thing the core fetches from
  // unpopulated memory. .4byte keeps the assembler from rejecting it.
  __asm__ volatile(".4byte 0x00000000");

  // Reaching here at all means the trap was taken and mret resumed us.
  if (!trap_seen) {
    // Unreachable in practice: without a trap the core wedges and the harness
    // times out rather than arriving here.
    exit(1);
  }
  if (trap_mcause != 2UL) {
    exit(1);
  }
  if (trap_mtval != 0UL) {
    exit(1);
  }
  // mepc must point into this function, at/after the auipc we just took.
  if (trap_mepc < before_pc || trap_mepc > before_pc + 64UL) {
    exit(1);
  }

  exit(0);
}
