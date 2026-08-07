// invariants.h — always-on microarchitectural assertions for the chiron core.
//
// WHY THIS EXISTS
// ---------------
// Four separate wedges in this repo turned out to be one defect: a completion
// landing on a ROB slot that speculation had already rolled back and
// REALLOCATED (the ACE responseBuffer clobber, speculative MMIO that was never
// squashed, the multiply pipeline's squash mis-pairing, and the divider's
// branchMask provenance clobber). Every one of them was found the same
// expensive way — days of hand-built, single-use probes hunting an event that
// fires roughly ONCE IN 27 MILLION CYCLES, and only after a benchmark had
// already visibly hung.
//
// That does not scale, and a hang is a terrible detector: it reports the
// symptom millions of cycles after the corruption, on whichever unlucky
// instruction happened to be downstream. The checks below assert the invariants
// those bugs violated, every cycle, in any workload — so the NEXT one is caught
// at the cycle it happens, with the offending port named, instead of being
// reverse-engineered from a stalled pipeline.
//
// These are host-side observers. They read Verilated state and never drive it,
// so enabling them cannot change RTL behaviour — only how loudly it complains.
//
// USAGE
//   #include "sim/harness/invariants.h"
//   chiron_invariants inv;  inv.attach(bench.raw());
//   ... each cycle:  tb->eval();  inv.tick(cyc);  // PRE-EDGE, see below
//                    tb->clock=1; tb->eval(); tb->clock=0; tb->eval();
//   ... at the end:  if (inv.report()) return 1;   // nonzero => a gate failed
//
// Compile with -DCHIRON_INVARIANTS to enable; without it every method is an
// empty inline and costs nothing, so harnesses can include it unconditionally.
//
// TWO SAMPLING RULES, both learned the hard way:
//
//  1. PRE-EDGE. Call tick() after eval() and BEFORE toggling the clock, so you
//     observe the values that are about to be committed to registers.
//  2. A write port asserting at cycle T makes its effect visible at T+1.
//     Transitions are therefore judged against the PREVIOUS cycle's port state
//     (the prev_* fields). Comparing against the same cycle turned ~394,000
//     legitimate resolutions into "anomalies" once; the corrected check found
//     exactly one real event in the same run. Do not "simplify" this away.
#pragma once

#include <cstdio>
#include <cstdint>
#include "Vsystem.h"

// ROB depth = 2^configuration.robAddrWidth. If you change robAddrWidth, also
// update Dcache/constants.scala:21 and the hardcoded 4.W in
// storeDataIssue.scala:126 — and this constant.
#ifndef CHIRON_ROB_DEPTH
#define CHIRON_ROB_DEPTH 16
#endif

// Cycles a hart may go without retiring anything before we call it wedged.
// Generous by default: legitimate boot phases (bbl's kernel copy, cold I-fetch
// through the CCU/L2) can stall a hart for a long time without being broken.
#ifndef CHIRON_WEDGE_CYCLES
#define CHIRON_WEDGE_CYCLES 2000000ULL
#endif

#ifndef CHIRON_INVARIANTS

// Disabled build: everything compiles away.
struct chiron_invariants {
  void attach(Vsystem *) {}
  void tick(uint64_t) {}
  int  report(bool = true) { return 0; }
  uint64_t violations() const { return 0; }
};

#else

class chiron_invariants {
 public:
  void attach(Vsystem *tb) { tb_ = tb; }

  void tick(uint64_t cyc) {
    if (!tb_) return;
    core_sample s;
#define CHIRON_INV_ONE(N) do { sample_core##N(s); check(N, s, cyc); } while (0)
    CHIRON_INV_ONE(0); CHIRON_INV_ONE(1);
    CHIRON_INV_ONE(2); CHIRON_INV_ONE(3);
#undef CHIRON_INV_ONE
  }

  uint64_t violations() const {
    uint64_t n = 0;
    for (int c = 0; c < 4; c++)
      n += st_[c].n_ready_no_resolve + st_[c].n_ready_outside_window +
           st_[c].n_double_resolve + st_[c].n_wedged;
    return n;
  }

  // Prints a summary. Returns nonzero if any invariant was violated, so a
  // harness can `return inv.report();` and have CI fail on corruption even when
  // the workload itself happened to produce the right answer.
  int report(bool verbose = true) {
    uint64_t total = violations();
    if (!verbose && !total) return 0;
    printf("\n=== chiron invariants ===\n");
    for (int c = 0; c < 4; c++) {
      const st &x = st_[c];
      if (!x.seen) continue;
      printf("  hart%d: branch-readied-without-resolution %llu%s  "
             "ready-outside-ROB-window %llu%s  double-resolve %llu%s  "
             "wedged %llu%s\n",
             c,
             (unsigned long long)x.n_ready_no_resolve, first(x.first_rnr),
             (unsigned long long)x.n_ready_outside_window, first(x.first_row),
             (unsigned long long)x.n_double_resolve, first(x.first_dr),
             (unsigned long long)x.n_wedged, first(x.first_wedge));
      if (x.n_ready_no_resolve)
        printf("        attributed to  ALU %llu   M-ext %llu   "
               "load/other %llu\n",
               (unsigned long long)x.by_alu, (unsigned long long)x.by_mext,
               (unsigned long long)x.by_other);
    }
    if (!total) { printf("  no violations\n"); return 0; }
    printf("  *** %llu VIOLATION(S) — this is silent state corruption, not a\n"
           "      performance artefact. See sim/harness/probes/ for the probes\n"
           "      that localise each one.\n", (unsigned long long)total);
    return 1;
  }

 private:
  struct core_sample {
    unsigned readPtr, writePtr, allocFired;
    unsigned bevValid, bevPassed, bevRob;
    unsigned aluValid, aluRob;
    unsigned mValid, mRob;
    unsigned loadValid;
    unsigned ready[CHIRON_ROB_DEPTH];
    uint32_t insn[CHIRON_ROB_DEPTH];
  };

  struct st {
    bool     seen = false;
    unsigned prevReady[CHIRON_ROB_DEPTH] = {};
    int      pendingResolve[CHIRON_ROB_DEPTH] = {};
    unsigned prevRead = 0xffff;
    uint64_t lastCommitCyc = 0;
    bool     wedgeReported = false;
    // previous-cycle port state (see sampling rule 2 at the top)
    unsigned pBev = 0, pBrob = 0, pAlloc = 0, pWrite = 0;
    unsigned pAluV = 0, pAluRob = 0, pMV = 0, pMRob = 0, pLoadV = 0;
    uint64_t n_ready_no_resolve = 0, first_rnr = 0;
    uint64_t n_ready_outside_window = 0, first_row = 0;
    uint64_t n_double_resolve = 0, first_dr = 0;
    uint64_t n_wedged = 0, first_wedge = 0;
    uint64_t by_alu = 0, by_mext = 0, by_other = 0;
  };

  static const char *first(uint64_t c) {
    static char buf[4][40]; static int k = 0;
    if (!c) return "";
    k = (k + 1) & 3;
    snprintf(buf[k], sizeof buf[k], " (first @%llu)", (unsigned long long)c);
    return buf[k];
  }

  // ROB entry layout: PC[101:38] | instruction[37:6] | prfDest[5:0].
  static uint32_t insn_of(const uint32_t *w) {
    uint64_t v = 0;
    for (int i = 0; i < 32; i++) {
      const int b = 6 + i;
      v |= (uint64_t)((w[b >> 5] >> (b & 31)) & 1u) << i;
    }
    return (uint32_t)v;
  }

  // Is slot `i` currently allocated? occupancy 0 is ambiguous (empty vs full)
  // without the fifo's own flag, so we decline to judge there rather than risk
  // a false positive — an assertion that cries wolf gets switched off, and then
  // it protects nothing.
  static bool in_window(unsigned i, unsigned rd, unsigned wr) {
    const unsigned occ = (wr - rd) & (CHIRON_ROB_DEPTH - 1);
    if (occ == 0) return true;
    return ((i - rd) & (CHIRON_ROB_DEPTH - 1)) < occ;
  }

  void check(int c, const core_sample &s, uint64_t cyc) {
    st &x = st_[c];
    x.seen = true;

    // Forward progress: readPtr advancing is a retirement.
    if (x.prevRead != 0xffff && s.readPtr != x.prevRead) x.lastCommitCyc = cyc;
    if (x.prevRead == 0xffff) x.lastCommitCyc = cyc;
    if (!x.wedgeReported && cyc > x.lastCommitCyc + CHIRON_WEDGE_CYCLES) {
      x.wedgeReported = true;
      x.n_wedged++; if (!x.first_wedge) x.first_wedge = cyc;
      printf("[cyc %llu] INVARIANT hart%d WEDGED: no retirement for %llu cycles."
             " ROB head slot=%u insn=%08x ready=%u (rd=%u wr=%u)\n",
             (unsigned long long)cyc, c,
             (unsigned long long)(cyc - x.lastCommitCyc), s.readPtr,
             s.insn[s.readPtr], s.ready[s.readPtr], s.readPtr, s.writePtr);
    }
    if (s.readPtr != x.prevRead) x.wedgeReported = false;

    // Track resolutions so we can spot a branch readied without one, and a
    // branch resolved twice with no allocation between.
    if (s.bevValid) {
      if (x.pendingResolve[s.bevRob]) {
        x.n_double_resolve++;
        if (!x.first_dr) x.first_dr = cyc;
        printf("[cyc %llu] INVARIANT hart%d DOUBLE-RESOLVE slot=%u pass=%u\n",
               (unsigned long long)cyc, c, s.bevRob, s.bevPassed);
      }
      x.pendingResolve[s.bevRob] = 1;
    }
    if (s.allocFired) x.pendingResolve[s.writePtr] = 0;

    for (unsigned i = 0; i < CHIRON_ROB_DEPTH; i++) {
      if (s.ready[i] && !x.prevReady[i]) {
        const bool named  = x.pBev && (x.pBrob == i);
        const bool alloc  = x.pAlloc && (x.pWrite == i);
        const bool aluHit = x.pAluV && (x.pAluRob == i);
        const bool mHit   = x.pMV && (x.pMRob == i);

        // V1: a conditional branch's ready bit has exactly ONE legitimate
        // writer (rob.scala:115-117, on branch.valid at branch.robAddr). Any
        // other port setting it means that port completed under a robAddr whose
        // slot has since been reallocated — the branch then retires before its
        // own resolution arrives and the ROB jams at the next head.
        if ((s.insn[i] & 0x7f) == 0x63 && !named && !alloc) {
          x.n_ready_no_resolve++;
          if (!x.first_rnr) x.first_rnr = cyc;
          if (aluHit) x.by_alu++; else if (mHit) x.by_mext++; else x.by_other++;
          printf("[cyc %llu] INVARIANT hart%d BRANCH-READY-NO-RESOLVE slot=%u "
                 "insn=%08x culprit=%s [alu{v=%u rob=%u} mext{v=%u rob=%u} "
                 "load{v=%u}]\n",
                 (unsigned long long)cyc, c, i, s.insn[i],
                 aluHit ? "M-ext? no: ALU" : mHit ? "M-ext"
                        : x.pLoadV ? "load (by elimination)" : "UNKNOWN",
                 x.pAluV, x.pAluRob, x.pMV, x.pMRob, x.pLoadV);
        }

        // V2: the same defect one step earlier, and not specific to branches —
        // no port may ready a slot that is not currently allocated. Catches the
        // reallocated-slot class before the new occupant happens to be a branch.
        if (!alloc && !in_window(i, s.readPtr, s.writePtr)) {
          x.n_ready_outside_window++;
          if (!x.first_row) x.first_row = cyc;
          printf("[cyc %llu] INVARIANT hart%d READY-OUTSIDE-ROB-WINDOW slot=%u "
                 "insn=%08x (rd=%u wr=%u) [alu{v=%u rob=%u} mext{v=%u rob=%u}]\n",
                 (unsigned long long)cyc, c, i, s.insn[i], s.readPtr,
                 s.writePtr, x.pAluV, x.pAluRob, x.pMV, x.pMRob);
        }
      }
      x.prevReady[i] = s.ready[i];
    }

    x.prevRead = s.readPtr;
    x.pBev = s.bevValid; x.pBrob = s.bevRob;
    x.pAlloc = s.allocFired; x.pWrite = s.writePtr;
    x.pAluV = s.aluValid; x.pAluRob = s.aluRob;
    x.pMV = s.mValid; x.pMRob = s.mRob;
    x.pLoadV = s.loadValid;
  }

// Verilator exposes each hart as a distinct member, so the sampler is generated
// per core rather than indexed.
#define CHIRON_DEFINE_SAMPLER(N)                                               \
  void sample_core##N(core_sample &s) {                                        \
    s.readPtr  = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__rob__DOT__fifo__DOT__readPtr;  \
    s.writePtr = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__rob__DOT__fifo__DOT__writePtr; \
    s.allocFired =                                                             \
        (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__rob_allocate_fired; \
    s.bevValid  = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__branchEvals_valid;  \
    s.bevPassed = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__branchEvals_passed; \
    s.bevRob    = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__branchEvals_robAddr;\
    s.aluValid  = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__singleCycleArithmeticResponse_valid;   \
    s.aluRob    = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__singleCycleArithmeticResponse_robAddr; \
    s.mValid    = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__extnMResponse_valid;   \
    s.mRob      = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__extnMResponse_robAddr; \
    s.loadValid = (unsigned)tb_->system__DOT__chiron__DOT__core##N##__DOT__memAccess_responseOut_valid; \
    for (unsigned i = 0; i < CHIRON_ROB_DEPTH; i++) {                          \
      s.ready[i] = (unsigned)(tb_->system__DOT__chiron__DOT__core##N##__DOT__rob__DOT__results__DOT__memReg[i][0] & 1u); \
      s.insn[i]  = insn_of((const uint32_t *)                                  \
          &tb_->system__DOT__chiron__DOT__core##N##__DOT__rob__DOT__fifo__DOT__memReg[i]); \
    }                                                                          \
  }
  CHIRON_DEFINE_SAMPLER(0)
  CHIRON_DEFINE_SAMPLER(1)
  CHIRON_DEFINE_SAMPLER(2)
  CHIRON_DEFINE_SAMPLER(3)
#undef CHIRON_DEFINE_SAMPLER

  Vsystem *tb_ = nullptr;
  st st_[4];
};

#endif  // CHIRON_INVARIANTS

// Drop-in replacement for tick_nodump() that samples the invariants at the only
// correct point: after eval() has settled the combinational logic, before the
// clock edge commits it. With CHIRON_INVARIANTS off, inv.tick() is an empty
// inline and this is byte-for-byte tick_nodump().
inline void tick_checked(Vsystem *tb, chiron_invariants &inv, uint64_t cyc) {
  tb->eval();
  inv.tick(cyc);
  tb->clock = 1;
  tb->eval();
  tb->clock = 0;
  tb->eval();
}
