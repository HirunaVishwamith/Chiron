// robready_probe.cpp — who set ROB slot 12's ready bit BEFORE its branch resolved?
//
// Chain of established facts (robhead_probe -> brvanish_probe -> this):
//   * mt-ipimux wedges with hart1's ROB head = `bne a2,a6` @0x80000540 (slot 14),
//     ready bit never set, ROB full, everything downstream follows.
//   * At cyc 26814118 a FAILING resolution named robAddr=12. It flushed the
//     scheduler (occ 8 -> 0) and, via the unqualified kill at core.scala:627-628
//     (`when(branchOps.valid){ when(!branchOps.passed){ branchEvals.valid := false }}`),
//     swallowed the head branch's own resolution one cycle later.
//   * Slot 12's occupant had RETIRED at ~26814115, three cycles earlier.
//
// A first guess was that branchEvals desynchronised from the prediction queues,
// since core.scala:621-623 takes robAddr from branchInstruction but branchMask
// from branchPCs(0) and passed from predictedPCs(0). That looks WRONG on
// re-reading: both queues shift under `.elsewhen(branchInstruction.valid)`
// (lines 693, 716), so they advance in lockstep with branchInstruction, and
// branchEvals samples all three in the same cycle. Working the timing back,
// branchEvals@26814118 was loaded from branchInstruction@26814117, so
// branchInstruction.robAddr really was 12 then. The resolution is LEGITIMATE.
//
// Which relocates the defect. A conditional branch's ROB ready bit has exactly
// one writer -- rob.scala:115-117, on branch.valid at branch.robAddr -- so slot
// 12 must not have been commit-ready at 26814115 unless something set that bit
// early. Candidates, all distinguishable here:
//   (a) an execPort completing with a STALE robAddr (same bug class, different
//       unit: rob.scala:120-126 writes ready at execPorts(i).robAddr),
//   (b) a DOUBLE resolution -- an earlier branchEvals already naming rob=12,
//   (c) the slot never really retired and readPtr moved for another reason.
//
// So: track every ready-bit transition for every slot, tag it with whether a
// resolution named that slot on that cycle, and flag
//   READY-NO-RESOLVE : ready 0->1 on a slot with no branchEvals naming it and
//                      no allocation -- i.e. an exec port claimed it,
//   DOUBLE-RESOLVE   : two resolutions naming one slot with no allocation between.
//
// PRE-EDGE sampling (see robmodify_probe.cpp).
//
// Build: make build/robready_probe.out
// Run  : WATCH=12 FROM=26813900 TO=26814400 build/robready_probe.out \
//            bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C(s) tb_->system__DOT__chiron__DOT__core1__DOT__##s

static uint64_t wbits(const uint32_t *w, int lo, int len) {
  uint64_t v = 0;
  for (int i = 0; i < len; i++) {
    const int b = lo + i;
    v |= (uint64_t)((w[b >> 5] >> (b & 31)) & 1u) << i;
  }
  return v;
}

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t FROM  = env64("FROM", 26813900ULL);
  const uint64_t TO    = env64("TO",   26814400ULL);
  const unsigned WATCH = (unsigned)env64("WATCH", 12ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  unsigned prevRdy[16] = {0};
  uint64_t lastResolveCyc[16] = {0};
  uint64_t lastAllocCyc[16] = {0};
  int      pendingResolve[16] = {0};   // resolution seen since last allocation
  uint64_t nReadyNoResolve = 0, nDoubleResolve = 0;
  uint64_t firstReadyNoResolve = 0, firstDoubleResolve = 0;
  unsigned prevRd = 99;
  // previous-cycle port state (see the off-by-one note at the transition check)
  unsigned prevBev = 0, prevBrob = 0, prevAllocF = 0, prevWr = 0;
  unsigned prevAluV = 0, prevAluRob = 0, prevMV = 0, prevMRob = 0, prevLoadV = 0;
  uint64_t nByAlu = 0, nByM = 0, nByLoad = 0, nByNone = 0;

  while (cyc < TO) {
    tb_->eval();

    const unsigned rd = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
    const unsigned wr = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
    const unsigned bev  = (unsigned)C(branchEvals_valid);
    const unsigned bpas = (unsigned)C(branchEvals_passed);
    const unsigned brob = (unsigned)C(branchEvals_robAddr);
    const unsigned allocF = (unsigned)C(rob_allocate_fired);
    const unsigned cmtR = (unsigned)C(rob_commit_ready);
    // rob_commit_fired is no longer emitted (Verilator inlines it away -- the
    // same net rtl_model.h's commit_fired_raw was removed for). A commit is
    // observable as readPtr advancing, which is what we actually care about.
    const unsigned cmtF = (rd != prevRd) ? 1u : 0u;

    unsigned rdy[16];
    for (int i = 0; i < 16; i++)
      rdy[i] = (unsigned)(C(rob__DOT__results__DOT__memReg)[i][0] & 1u);

    if (allocF) { lastAllocCyc[wr] = cyc; pendingResolve[wr] = 0; }

    if (bev) {
      if (pendingResolve[brob]) {
        nDoubleResolve++;
        if (!firstDoubleResolve) firstDoubleResolve = cyc;
        if (cyc >= FROM && cyc <= TO)
          printf("[cyc %llu] ** DOUBLE-RESOLVE rob=%u (previous @%llu, no "
                 "allocation between; alloc @%llu) pass=%u\n",
                 (unsigned long long)cyc, brob,
                 (unsigned long long)lastResolveCyc[brob],
                 (unsigned long long)lastAllocCyc[brob], bpas);
      }
      pendingResolve[brob] = 1;
      lastResolveCyc[brob] = cyc;
    }

    // ready-bit transitions.
    // OFF-BY-ONE, do not "simplify" this: a write port asserting at cycle T
    // makes the ready bit visible at T+1, so a 0->1 seen now must be judged
    // against the PREVIOUS cycle's port state, not this one. Comparing against
    // the current cycle counted ~394k legitimate resolutions as anomalies.
    for (int i = 0; i < 16; i++) {
      if (rdy[i] && !prevRdy[i]) {
        const bool namedNow  = prevBev && (prevBrob == (unsigned)i);
        const bool allocNow  = prevAllocF && (prevWr == (unsigned)i);
        const bool aluNamed  = prevAluV && (prevAluRob == (unsigned)i);
        const bool mNamed    = prevMV   && (prevMRob   == (unsigned)i);
        const uint32_t *m = (const uint32_t *)&C(rob__DOT__fifo__DOT__memReg)[i];
        const uint32_t insn = (uint32_t)wbits(m, 6, 32);
        const bool isBranch = (insn & 0x7f) == 0x63;
        if (isBranch && !namedNow && !allocNow) {
          nReadyNoResolve++;
          if (aluNamed) nByAlu++; else if (mNamed) nByM++;
          else if (prevLoadV) nByLoad++; else nByNone++;
          if (!firstReadyNoResolve) firstReadyNoResolve = cyc;
          if (cyc >= FROM && cyc <= TO)
            printf("[cyc %llu] ** READY-NO-RESOLVE slot=%2u insn=%08x (BRANCH)"
                   " -- no branchEvals named it. culprit: %s "
                   "[alu{v=%u rob=%u} mext{v=%u rob=%u} load{v=%u}] rd=%u wr=%u\n",
                   (unsigned long long)cyc, i, insn,
                   aluNamed ? "ALU port" : mNamed ? "M-ext port"
                            : prevLoadV ? "LOAD port (by elimination)"
                                        : "UNKNOWN (no port valid!)",
                   prevAluV, prevAluRob, prevMV, prevMRob, prevLoadV, rd, wr);
        }
        if (cyc >= FROM && cyc <= TO && i == (int)WATCH)
          printf("[cyc %llu] slot%u READY 0->1  insn=%08x namedByResolve=%d "
                 "alloc=%d  rd=%u wr=%u\n",
                 (unsigned long long)cyc, i, insn, namedNow, allocNow, rd, wr);
      }
      if (!rdy[i] && prevRdy[i] && cyc >= FROM && cyc <= TO && i == (int)WATCH)
        printf("[cyc %llu] slot%u READY 1->0  rd=%u wr=%u\n",
               (unsigned long long)cyc, i, rd, wr);
      prevRdy[i] = rdy[i];
    }

    if (cyc >= FROM && cyc <= TO && (bev || cmtF)) {
      const uint32_t *hm = (const uint32_t *)&C(rob__DOT__fifo__DOT__memReg)[rd];
      printf("[cyc %llu] %s%s rob=%2u pass=%u | rd=%2u wr=%2u cmtRdy=%u "
             "headInsn=%08x rdy[%u]=%u\n",
             (unsigned long long)cyc, bev ? "RESOLVE " : "", cmtF ? "COMMIT" : "",
             brob, bpas, rd, wr, cmtR, (uint32_t)wbits(hm, 6, 32),
             WATCH, rdy[WATCH]);
    }

    prevRd = rd;
    prevBev = bev; prevBrob = brob; prevAllocF = allocF; prevWr = wr;
    prevAluV   = (unsigned)C(singleCycleArithmeticResponse_valid);
    prevAluRob = (unsigned)C(singleCycleArithmeticResponse_robAddr);
    prevMV     = (unsigned)C(extnMResponse_valid);
    prevMRob   = (unsigned)C(extnMResponse_robAddr);
    prevLoadV  = (unsigned)C(memAccess_responseOut_valid);

    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
  }

  printf("\n=== summary over %llu cycles (hart1) ===\n", (unsigned long long)cyc);
  printf("  READY-NO-RESOLVE (branch slot made ready with no resolution): %llu"
         " (first @%llu)\n",
         (unsigned long long)nReadyNoResolve, (unsigned long long)firstReadyNoResolve);
  printf("      attributed to  ALU port : %llu\n", (unsigned long long)nByAlu);
  printf("      attributed to  M-ext    : %llu\n", (unsigned long long)nByM);
  printf("      attributed to  LOAD     : %llu\n", (unsigned long long)nByLoad);
  printf("      no port valid (!)       : %llu\n", (unsigned long long)nByNone);
  printf("  DOUBLE-RESOLVE   (two resolutions, no allocation between)   : %llu"
         " (first @%llu)\n",
         (unsigned long long)nDoubleResolve, (unsigned long long)firstDoubleResolve);
  printf("\nReading: READY-NO-RESOLVE>0 => an exec port writes the ROB ready bit\n"
         "under a stale/wrong robAddr, retiring a branch before it resolves.\n"
         "DOUBLE-RESOLVE>0 => the branch unit emits a resolution twice.\n");
  return 0;
}
