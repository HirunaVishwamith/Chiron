// ipitmr_ctx_probe.cpp — capture the FULL hart0 commit stream across the point
// where the csd-clear loop counter a4 gets corrupted, to settle what actually
// destroys it.
//
// Established by ipitmr_loop_probe: in the failing iteration a4 goes 3 -> 0x290
// across a ~250-cycle window in which nothing in the loop body commits. 0x290 is
// the value of timer_irqs[me], which only the TIMER handler ever puts in a4
// (ld a4,416(a5); addi a4,a4,1 at 0x3dc/0x3e0). So a timer trap ran mid-loop and
// a4 came back wrong instead of restored.
//
// The trap stub is:
//     290: sd a4,96(sp)        <- save
//     2a0: jal trap_isr
//     2d4: ld a4,96(sp)        <- restore
//     2e8: mret
// so the two candidate faults are:
//   (a) the `ld a4,96(sp)` at 0x2d4 never wrote back (lost load result); or
//   (b) it wrote back STALE memory — the value from an EARLIER trap frame at the
//       same sp (which is exactly where a previous timer ISR's a4 would sit),
//       i.e. a store->load ordering/forwarding violation against 0x290's store.
// Both look identical from the register file alone; the discriminator is the
// commit stream plus sp/mepc/mcause around the trap.
//
// Records every hart0 commit-PC change (no region filter) with a4/a6/sp and the
// trap CSRs, and dumps the ring the first time the loop bne is latched with an
// a4 already past the bound.
//
// Build: make build/ipitmr_ctx_probe.out
// Run  : build/ipitmr_ctx_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

static const uint32_t INSN_BNE = 0xff0710e3u;   // bne a4,a6,454

struct Ev {
  uint64_t cyc, pc, a4, a6, sp, mepc, mcause, mstatus;
  uint64_t rs1;
  int injSt, brCnt, latch;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END  = env64("END", 200000000ULL);
  const int      KEEP = (int)env64("KEEP", 400);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  static const int RING = 32768;
  static Ev ring[RING];
  int head = 0; uint64_t nev = 0;
  uint64_t cyc = 0, prev_pc = ~0ULL;
  uint32_t prev_bi_valid = 0;
  bool fired = false;

  auto push = [&](int latch) {
    Ev &e = ring[head];
    e.cyc = cyc; e.pc = tb_->robOut0_pc;
    e.a4 = tb_->registersOut0_14; e.a6 = tb_->registersOut0_16;
    e.sp = tb_->registersOut0_2;
    e.mepc    = CORE_RAW(0, decode__DOT__mepc);
    e.mcause  = CORE_RAW(0, decode__DOT__mcause);
    e.mstatus = CORE_RAW(0, decode__DOT__mstatus);
    e.rs1     = CORE_RAW(0, branchInstruction_rs1);
    e.injSt = (int)CORE_RAW(0, interruptInjectStatus);
    e.brCnt = (int)CORE_RAW(0, branchCounter);
    e.latch = latch;
    head = (head + 1) % RING; ++nev;
  };

  while (cyc < END && !fired) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const uint64_t pc = tb_->robOut0_pc;
    if (pc != prev_pc) push(0);
    prev_pc = pc;

    const uint32_t biv  = CORE_RAW(0, branchInstruction_valid);
    const uint32_t insn = (uint32_t)CORE_RAW(0, branchInstruction_instruction);
    if (biv && !prev_bi_valid && insn == INSN_BNE) {
      push(1);
      const uint64_t rs1 = CORE_RAW(0, branchInstruction_rs1);
      if (rs1 >= 5 && rs1 < 0x100000ULL) fired = true;
    }
    prev_bi_valid = biv;
  }

  if (!fired) { printf("no trigger by cyc=%llu\n", (unsigned long long)cyc); return 0; }

  printf("TRIGGER cyc=%llu\n\n", (unsigned long long)cyc);
  printf("%-11s %-4s %-10s %-9s %-5s %-12s %-10s %-6s %-10s %s\n",
         "cyc", "what", "commitPC", "a4", "a6", "sp", "mepc", "mcause", "mstatus",
         "br.rs1 inj/brCnt");
  int n = (int)(nev < (uint64_t)KEEP ? nev : (uint64_t)KEEP);
  for (int i = n; i > 0; --i) {
    const Ev &e = ring[(head - i + RING) % RING];
    printf("%-11llu %-4s %-10llx %-9llx %-5llx %-12llx %-10llx %-6llx %-10llx %llx %d/%d\n",
           (unsigned long long)e.cyc, e.latch ? "BNE" : "cmt",
           (unsigned long long)e.pc, (unsigned long long)e.a4,
           (unsigned long long)e.a6, (unsigned long long)e.sp,
           (unsigned long long)e.mepc, (unsigned long long)e.mcause,
           (unsigned long long)e.mstatus, (unsigned long long)e.rs1,
           e.injSt, e.brCnt);
  }
  return 0;
}
