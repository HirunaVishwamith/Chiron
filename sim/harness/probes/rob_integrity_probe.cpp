// rob_integrity_probe.cpp — trace the hart0 ROB pointers across the point where
// mt-ipitmr's csd-clear loop counter is destroyed.
//
// Evidence so far (ipitmr_ctx_probe): right before a4 is clobbered, the hart0
// commit-PC stream goes non-monotonic —
//     0x464 -> 0x470 -> 0x468 -> 0x474 -> 0x46c -> 0x470
// showing BOTH sides of the `beqz a0,0x470` at 0x464 reaching the ROB head, and
// a4 ends up holding 0x290 (a value only an older trap frame produced). No trap
// occurred in that window: sp/mepc/mcause/mstatus are constant.
//
// The mechanism that fits is the mispredict rewind of the ROB write pointer
// (rob.scala:105-106 — `fifo.modify := branch.valid & !branch.pass`,
// `fifo.modifyVal := branch.robAddr`). If the rewind target is wrong the write
// pointer can land at/behind the read pointer, so later allocations overwrite
// entries that have not committed — the head slot mutates under the commit
// logic and stale instructions retire, landing their register writes.
//
// So: record readPtr/writePtr/empty/full together with the commit pc, commitFired
// and the flush controls every cycle, and dump the ring when the loop's
// `bne a4,a6` is finally latched with an a4 already past the bound.
//
// Build: make build/rob_integrity_probe.out
// Run  : build/rob_integrity_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig) tb_->system__DOT__chiron__DOT__core0__DOT__##sig

static const uint32_t INSN_BNE = 0xff0710e3u;   // bne a4,a6,454

struct Ev {
  uint64_t cyc, pc, a4;
  unsigned rd, wr, empty, full, fired;
  unsigned beV, beP, beRob, biV, biRob, inj, brCnt;
  uint64_t beNext;
  unsigned latch;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END  = env64("END", 200000000ULL);
  const int      KEEP = (int)env64("KEEP", 300);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  static const int RING = 65536;
  static Ev ring[RING];
  int head = 0; uint64_t nev = 0;
  uint64_t cyc = 0, prev_pc = ~0ULL;
  unsigned prev_rd = ~0u, prev_wr = ~0u;
  uint32_t prev_biv = 0;
  bool fired_trig = false;

  auto push = [&](unsigned latch) {
    Ev &e = ring[head];
    e.cyc = cyc; e.pc = tb_->robOut0_pc; e.a4 = tb_->registersOut0_14;
    e.rd = (unsigned)C0(rob__DOT__fifo__DOT__readPtr);
    e.wr = (unsigned)C0(rob__DOT__fifo__DOT__writePtr);
    e.empty = (unsigned)C0(rob__DOT__fifo__DOT__emptyReg);
    e.full  = (unsigned)C0(rob__DOT__fifo__DOT__fullReg);
    e.fired = (unsigned)tb_->robOut0_commitFired;
    e.beV = (unsigned)C0(branchEvals_valid);
    e.beP = (unsigned)C0(branchEvals_passed);
    e.beRob = (unsigned)C0(branchEvals_robAddr);
    e.beNext = (uint64_t)C0(branchEvals_nextPC);
    e.biV = (unsigned)C0(branchInstruction_valid);
    e.biRob = (unsigned)C0(branchInstruction_robAddr);
    e.inj = (unsigned)C0(interruptInjectStatus);
    e.brCnt = (unsigned)C0(branchCounter);
    e.latch = latch;
    head = (head + 1) % RING; ++nev;
  };

  while (cyc < END && !fired_trig) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const uint64_t pc = tb_->robOut0_pc;
    const unsigned rd = (unsigned)C0(rob__DOT__fifo__DOT__readPtr);
    const unsigned wr = (unsigned)C0(rob__DOT__fifo__DOT__writePtr);
    // Record whenever the head, the pointers, or a flush moves.
    if (pc != prev_pc || rd != prev_rd || wr != prev_wr ||
        C0(branchEvals_valid)) push(0);
    prev_pc = pc; prev_rd = rd; prev_wr = wr;

    const uint32_t biv  = (uint32_t)C0(branchInstruction_valid);
    const uint32_t insn = (uint32_t)C0(branchInstruction_instruction);
    if (biv && !prev_biv && insn == INSN_BNE) {
      push(1);
      const uint64_t rs1 = (uint64_t)C0(branchInstruction_rs1);
      if (rs1 >= 5 && rs1 < 0x100000ULL) fired_trig = true;
    }
    prev_biv = biv;
  }

  if (!fired_trig) { printf("no trigger by cyc=%llu\n", (unsigned long long)cyc); return 0; }

  printf("TRIGGER cyc=%llu\n\n", (unsigned long long)cyc);
  printf("%-11s %-4s %-10s %-8s %-3s %-3s %-2s %-2s %-4s %-16s %-10s %-8s %s\n",
         "cyc","what","headPC","a4","rd","wr","e","f","fire",
         "beV/beP/beRob","beNextPC","biV/biRob","inj/brCnt");
  int n = (int)(nev < (uint64_t)KEEP ? nev : (uint64_t)KEEP);
  for (int i = n; i > 0; --i) {
    const Ev &e = ring[(head - i + RING) % RING];
    printf("%-11llu %-4s %-10llx %-8llx %-3u %-3u %-2u %-2u %-4u %u/%u/%-12u %-10llx %u/%-6u %u/%u\n",
           (unsigned long long)e.cyc, e.latch ? "BNE" : "-",
           (unsigned long long)e.pc, (unsigned long long)e.a4,
           e.rd, e.wr, e.empty, e.full, e.fired,
           e.beV, e.beP, e.beRob, (unsigned long long)e.beNext,
           e.biV, e.biRob, e.inj, e.brCnt);
  }
  return 0;
}
