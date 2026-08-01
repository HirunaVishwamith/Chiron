// robmodify_probe.cpp — what does the ROB rollback ACTUALLY see?
//
// Previous attempt (brleak_probe) sampled rob.fifo.readPtr/writePtr AFTER the
// clock edge and inferred the rollback inputs from them. That inference was
// wrong: a guard derived from it (doModify = modify && relTarget < occupancy)
// generated correct Verilog yet produced a BIT-IDENTICAL 27M-cycle run, so
// modifyInWindow never fired — meaning the pointers the FIFO actually saw were
// not the ones I read.
//
// This probe fixes the methodology: it samples everything PRE-EDGE, i.e. after
// eval() has settled the combinational logic but BEFORE clock is toggled. Those
// are precisely the values that drive the register update, so
//   post-edge writePtr == nextval  <=>  the modify path fired
// becomes a checkable identity rather than an assumption.
//
// robFifo (pipeline/Fifo.scala) computes:
//   modify    = branch.valid & !branch.pass      (rob.scala:105)
//   modifyVal = branch.robAddr                   (rob.scala:106)
//   nextval   = (modifyVal == depth-1) ? 0 : modifyVal + 1
//   when(modify) { writeReg := nextval; fullReg := nextval === readPtr;
//                  emptyReg := false }
//   when(flushAll) { writeReg := readReg; fullReg := false; emptyReg := true }
// and rob.branch is driven from core.scala's branchEvals, which IS visible.
// flushAll (= coherentLoadInvalidReg) is not visible, so it is identified from
// the observed transition instead (writePtr -> readPtr with empty set).
//
// Reported per rollback:
//   occ   = live entries before the rollback, (writePtr-readPtr) mod depth with
//           the empty/full flags disambiguating
//   rel   = (modifyVal - readPtr) mod depth, i.e. how far the target is from the
//           commit point
//   rel >= occ  =>  the target is ALREADY RETIRED and the rollback is bogus
// plus the actual pointer/flag transition, so "did the rollback grow the ROB"
// is read off directly rather than derived.
//
// Build: make build/robmodify_probe.out
// Run  : FROM=26813900 TO=26814400 build/robmodify_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C(s) tb_->system__DOT__chiron__DOT__core1__DOT__##s

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t FROM  = env64("FROM", 26813900ULL);
  const uint64_t TO    = env64("TO",   26814400ULL);
  const unsigned DEPTH = (unsigned)env64("DEPTH", 16ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  uint64_t n_modify = 0, n_out_of_window = 0, n_grew = 0;
  uint64_t first_oow = 0, first_grew = 0;

  while (cyc < TO) {
    tb_->eval();   // settle combinational logic with the CURRENT register state

    // ---------- PRE-EDGE: exactly what the register update will consume -----
    const unsigned rd    = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
    const unsigned wr    = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
    const unsigned emp   = (unsigned)C(rob__DOT__fifo__DOT__emptyReg);
    const unsigned ful   = (unsigned)C(rob__DOT__fifo__DOT__fullReg);
    const unsigned bev   = (unsigned)C(branchEvals_valid);
    const unsigned bpas  = (unsigned)C(branchEvals_passed);
    const unsigned brob  = (unsigned)C(branchEvals_robAddr);
    const unsigned enqR  = (unsigned)C(rob__DOT__fifo_io_enq_ready);
    const unsigned deqV  = (unsigned)C(rob__DOT__fifo_io_deq_valid);
    const unsigned allocF= (unsigned)C(rob_allocate_fired);
    const unsigned cmtR  = (unsigned)C(rob_commit_ready);

    const unsigned modify    = bev && !bpas;
    const unsigned modifyVal = brob;
    const unsigned nextval   = (modifyVal == DEPTH - 1) ? 0u : modifyVal + 1u;
    const unsigned occ = emp ? 0u : (ful ? DEPTH : ((wr - rd) & (DEPTH - 1)));
    const unsigned rel = (modifyVal - rd) & (DEPTH - 1);
    const unsigned outOfWindow = modify && (rel >= occ);

    // ---------- clock the edge ----------
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    // ---------- POST-EDGE: what actually happened ----------
    const unsigned rd2  = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
    const unsigned wr2  = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
    const unsigned emp2 = (unsigned)C(rob__DOT__fifo__DOT__emptyReg);
    const unsigned ful2 = (unsigned)C(rob__DOT__fifo__DOT__fullReg);
    const unsigned occ2 = emp2 ? 0u : (ful2 ? DEPTH : ((wr2 - rd2) & (DEPTH - 1)));

    if (modify) {
      n_modify++;
      if (outOfWindow) { if (!n_out_of_window) first_oow = cyc; n_out_of_window++; }
      if (occ2 > occ)  { if (!n_grew) first_grew = cyc; n_grew++; }
    }

    if (cyc >= FROM && cyc <= TO && (modify || wr2 != wr || rd2 != rd)) {
      const char *path = (modify && wr2 == nextval) ? "MODIFY"
                       : (wr2 == rd2 && emp2)       ? "flushAll"
                       : (wr2 != wr)                ? "enq"
                       : "-";
      printf("[cyc %llu] pre{rd=%2u wr=%2u e=%u f=%u occ=%2u} "
             "brEval{v=%u pass=%u rob=%2u} modify=%u mVal=%2u nextval=%2u "
             "rel=%2u %s | post{rd=%2u wr=%2u e=%u f=%u occ=%2u} took=%s "
             "enqRdy=%u deqV=%u alloc=%u cmtRdy=%u%s\n",
             (unsigned long long)cyc, rd, wr, emp, ful, occ,
             bev, bpas, brob, modify, modifyVal, nextval, rel,
             outOfWindow ? "OUT-OF-WINDOW" : (modify ? "in-window" : ""),
             rd2, wr2, emp2, ful2, occ2, path,
             enqR, deqV, allocF, cmtR,
             (occ2 > occ && modify) ? "  <<< ROLLBACK GREW THE ROB" : "");
    }
  }

  printf("\n=== rollback summary over %llu cycles (hart1, depth=%u) ===\n",
         (unsigned long long)cyc, DEPTH);
  printf("  rollbacks (modify fired)      : %llu\n", (unsigned long long)n_modify);
  printf("  target already retired        : %llu (first @%llu)\n",
         (unsigned long long)n_out_of_window, (unsigned long long)first_oow);
  printf("  rollback INCREASED occupancy  : %llu (first @%llu)\n",
         (unsigned long long)n_grew, (unsigned long long)first_grew);
  return 0;
}
