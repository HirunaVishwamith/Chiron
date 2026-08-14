// dualalloc_probe.cpp — test the rename invariant directly: is one physical
// register handed to two live instructions?
//
// Why this probe exists. mt-ipitmr's a4 is destroyed at cyc 9067755 by a write
// to PRF[33] while p33 is still a4's committed mapping (map(14)==33) and its
// free bit is 0. Earlier attempts to pin the writer were inconclusive:
//   * the ACE MSHR holds entries with core_prfDest==33, but branch.valid==0 is
//     ambiguous between "squashed" and "never speculative"; and
//   * counting freeRegAddr==33 events proves nothing, because freeRegAddr is a
//     combinational PriorityEncoder(PRFFreeList) (decode.scala:335) that
//     flickers whenever the free list moves — it is not a consumption signal.
//
// The unambiguous test is the invariant itself: a physical register must not be
// issued as outputBuffer.PRFDest twice without its free bit being set in
// between. decode.scala:215 captures PRFDest under only
// `validInputBuf && readyOutputBuf`, whereas the matching
// `PRFFreeList(freeRegAddr) := false` at :353-358 is further gated on the
// instruction type, rd =/= 0, AND `!branchEvalIn.fired || branchEvalIn.passFail`
// — so a mispredict cycle can let an instruction leave decode owning a register
// that was never marked allocated. This probe detects that directly.
//
// Reports every re-issue of a PRFDest that is still owned (free bit == 0) since
// its previous issue, with the instruction words of both owners.
//
// Build: make build/dualalloc_probe.out
// Run  : build/dualalloc_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig) tb_->system__DOT__chiron__DOT__core0__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t STOP   = env64("STOP", 9067800ULL);
  const int      MAXHIT = (int)env64("MAXHIT", 40);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  // Per physical register: cycle + instruction of the last issue, and whether it
  // has been returned to the free list since.
  uint64_t last_cyc[64] = {0}, last_insn[64] = {0}, last_pc[64] = {0};
  bool     owned[64]    = {false};

  uint64_t cyc = 0; int hits = 0;
  uint64_t prev_pd = ~0ULL; unsigned prev_fire = 0;

  auto freebit = [&](unsigned i) -> unsigned {
    // Only the handful of PRFFreeList bits Verilator keeps as named regs are
    // reachable; index 33 is the one this bug lands on, so track it exactly and
    // fall back to "unknown" (treated as freed) elsewhere.
    if (i == 33) return (unsigned)C0(decode__DOT__PRFFreeList_33);
    return 2u;
  };

  while (cyc < STOP && hits < MAXHIT) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const unsigned vin  = (unsigned)C0(decode__DOT__validInputBuf);
    const unsigned rout = (unsigned)C0(decode__DOT__readyOutputBuf);
    const uint64_t pd   = (uint64_t)C0(decode__DOT__outputBuffer_PRFDest);
    const uint64_t insn = (uint64_t)C0(decode__DOT__outputBuffer_instruction);
    const uint64_t pc   = (uint64_t)C0(decode__DOT__outputBuffer_pc);

    // An issue is decode.scala:212's capture actually taking effect.
    const bool issued = vin && rout && (pd != prev_pd || !prev_fire);
    if (issued && pd < 64) {
      const unsigned f = freebit((unsigned)pd);
      if (owned[pd] && f == 0) {
        ++hits;
        printf("[hit %d] cyc=%llu  p%llu RE-ISSUED while still owned (free=%u)\n",
               hits, (unsigned long long)cyc, (unsigned long long)pd, f);
        printf("         prev owner: cyc=%llu insn=%08llx pc=%llx\n",
               (unsigned long long)last_cyc[pd],
               (unsigned long long)last_insn[pd], (unsigned long long)last_pc[pd]);
        printf("         new  owner: cyc=%llu insn=%08llx pc=%llx\n",
               (unsigned long long)cyc, (unsigned long long)insn,
               (unsigned long long)pc);
      }
      last_cyc[pd] = cyc; last_insn[pd] = insn; last_pc[pd] = pc;
      owned[pd] = true;
    }
    prev_pd = pd; prev_fire = vin && rout;

    // Returning to the free list clears ownership.
    if (freebit(33) == 1) owned[33] = false;
  }
  printf("\ndone cyc=%llu hits=%d\n", (unsigned long long)cyc, hits);
  return 0;
}
