// ipitmr_loop_probe.cpp — pin down WHY mt-ipitmr's csd-clear loop in trap_isr
// never terminates on hart0.
//
// The loop (from mt-ipitmr.riscv.dump) is:
//     440: li    a4,0            <- loop counter s = 0
//     450: li    a6,4            <- bound
//     454: ...                   <- body (reads csd[me][s], clears it)
//     470: addiw a4,a4,1
//     474: bne   a4,a6,454       <- exits when a4 == 4
//
// At the wedge a4 is ~0x33ab and still growing with a6 == 4, i.e. the loop ran
// straight past 4. Exactly two things can cause that:
//   (a) the `li a4,0` at 0x440 never landed (lost register write), so the loop
//       started above 4 and can only exit after 2^32 iterations; or
//   (b) `li a4,0` did land, but ONE resolution of the bne at 0x474 was wrong on
//       the iteration where a4 became 4 — e.g. the branch evaluated a stale
//       pre-`addiw` a4 (=3) — so it was taken when it should have fallen
//       through, and a4 then runs away forever.
//
// This probe records a ring of loop-relevant events on hart0 and dumps it the
// first time the bne is evaluated with rs1 >= 5 (a4 already past the bound).
// The ring shows, for every earlier iteration, the architectural a4 and the
// rs1/rs2 the branch actually compared — which discriminates (a) from (b)
// directly instead of by inference.
//
// Build: make build/ipitmr_loop_probe.out
// Run  : build/ipitmr_loop_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define PERIPH(sig) tb_->system__DOT__peripherals__DOT__##sig
#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

// Loop landmarks (physical addresses = 0x80000000 + file offset).
static const uint64_t PC_LI_A4   = 0x80000440ULL;  // li a4,0
static const uint64_t PC_LI_A6   = 0x80000450ULL;  // li a6,4
static const uint64_t PC_BODY    = 0x80000454ULL;  // loop body head
static const uint64_t PC_ADDIW   = 0x80000470ULL;  // addiw a4,a4,1
static const uint64_t PC_BNE     = 0x80000474ULL;  // bne a4,a6,454
static const uint64_t PC_AFTER   = 0x80000478ULL;  // fall-through
static const uint32_t INSN_BNE   = 0xff0710e3u;    // the bne encoding

struct Ev {
  uint64_t cyc;
  const char *kind;
  uint64_t pc;       // commit pc (robOut0_pc)
  uint64_t a4;       // architectural a4 (x14)
  uint64_t a6;       // architectural a6 (x16)
  uint64_t rs1, rs2; // branch operands as captured by branchInstruction
  uint64_t nextPC;
  int passed, evalV, injSt, brCnt;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END  = env64("END", 200000000ULL);
  const int      KEEP = (int)env64("KEEP", 260);   // events to print at trigger

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  static const int RING = 8192;
  static Ev ring[RING];
  int head = 0; uint64_t nev = 0;

  uint64_t cyc = 0, prev_pc = 0;
  uint32_t prev_bi_valid = 0;
  bool fired = false;

  auto push = [&](const char *kind) {
    Ev &e = ring[head];
    e.cyc  = cyc; e.kind = kind;
    e.pc   = tb_->robOut0_pc;
    e.a4   = tb_->registersOut0_14;
    e.a6   = tb_->registersOut0_16;
    e.rs1  = CORE_RAW(0, branchInstruction_rs1);
    e.rs2  = CORE_RAW(0, branchInstruction_rs2);
    e.nextPC = CORE_RAW(0, branchEvals_nextPC);
    e.passed = (int)CORE_RAW(0, branchEvals_passed);
    e.evalV  = (int)CORE_RAW(0, branchEvals_valid);
    e.injSt  = (int)CORE_RAW(0, interruptInjectStatus);
    e.brCnt  = (int)CORE_RAW(0, branchCounter);
    head = (head + 1) % RING; ++nev;
  };

  while (cyc < END && !fired) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const uint64_t pc = tb_->robOut0_pc;
    const uint32_t bi_valid = CORE_RAW(0, branchInstruction_valid);
    const uint32_t bi_insn  = (uint32_t)CORE_RAW(0, branchInstruction_instruction);

    // Commit-pc movement inside the loop region.
    if (pc != prev_pc && pc >= PC_LI_A4 && pc <= PC_AFTER) {
      const char *k = (pc == PC_LI_A4) ? "commit li-a4"
                    : (pc == PC_LI_A6) ? "commit li-a6"
                    : (pc == PC_ADDIW) ? "commit addiw"
                    : (pc == PC_BNE)   ? "commit bne"
                    : (pc == PC_BODY)  ? "commit body"
                    : (pc == PC_AFTER) ? "commit after" : "commit ...";
      push(k);
    }
    prev_pc = pc;

    // The bne being latched into branchInstruction with its operands.
    if (bi_valid && !prev_bi_valid && bi_insn == INSN_BNE) {
      push("BNE latch");
      // Trigger: the branch is comparing an a4 that is already past the bound.
      const uint64_t rs1 = CORE_RAW(0, branchInstruction_rs1);
      if (rs1 >= 5 && rs1 < 0x100000ULL) fired = true;
    }
    prev_bi_valid = bi_valid;

    // Every branch resolution (gives passed/nextPC for the latch above).
    if (CORE_RAW(0, branchEvals_valid)) push("  eval");
  }

  if (!fired) { printf("no trigger by cyc=%llu\n", (unsigned long long)cyc); return 0; }

  printf("TRIGGER at cyc=%llu (bne latched with rs1 >= 5)\n\n",
         (unsigned long long)cyc);
  printf("%-12s %-13s %-10s %-8s %-6s %-10s %-10s %-10s %s\n",
         "cyc", "kind", "commitPC", "a4", "a6", "br.rs1", "br.rs2", "nextPC",
         "pass/evalV/inj/brCnt");
  int n = (int)(nev < (uint64_t)KEEP ? nev : (uint64_t)KEEP);
  for (int i = n; i > 0; --i) {
    const Ev &e = ring[(head - i + RING) % RING];
    printf("%-12llu %-13s %-10llx %-8llx %-6llx %-10llx %-10llx %-10llx %d/%d/%d/%d\n",
           (unsigned long long)e.cyc, e.kind, (unsigned long long)e.pc,
           (unsigned long long)e.a4, (unsigned long long)e.a6,
           (unsigned long long)e.rs1, (unsigned long long)e.rs2,
           (unsigned long long)e.nextPC, e.passed, e.evalV, e.injSt, e.brCnt);
  }
  return 0;
}
