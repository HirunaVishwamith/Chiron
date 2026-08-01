// brleak_probe.cpp — where does hart1's branchCounter leak?
//
// injwedge_probe established the wedge state precisely (mt-ipimux, cyc ~27.2M):
//   interruptInjectStatus = waitToInjectInterr   (stuck since 26815311)
//   branchCounter         = 1
//   scheduler queue       = ALL 8 ENTRIES INVALID
//   rob.fifo.empty        = 0
//   branchInstruction     = valid=0
//
// So the count is a PHANTOM. core.scala:1102-1106 only decrements branchCounter
// on branchEvals.valid, branchEvals.valid only comes from branchInstruction.valid
// (core.scala:621), and branchInstruction.valid only comes from an instruction
// issuing out of the scheduler (core.scala:595). With the scheduler empty and
// core.scala:1197 holding fetch+decode down for as long as the FSM sits in
// waitToInjectInterr with branchCounter != 0, nothing can ever refill it:
//
//   exit (a) branchCounter == 0                -> unreachable, nothing decrements
//   exit (b) branchInstruction.valid && cond-br -> unreachable, nothing issues
//
// ...and the hart never takes its IPI. Permanent.
//
// The remaining question is how a branch got counted at decode and then left the
// pipeline without ever producing a branchEvals. A branch removed by a squash is
// normally accounted for because any failing resolution zeroes the whole counter
// (core.scala:1108-1111) — so the leak must be a squash path that drops a
// scheduler entry WITHOUT a coincident failing branchEvals.
//
// This probe watches a window around the freeze and prints every cycle in which
// the counter, the scheduler occupancy, the FSM state or a resolution changes.
// It flags the specific event of interest:
//   BRANCH-VANISHED : a scheduler entry with isBranch=1 goes valid -> invalid on
//                     a cycle with no branchEvals.valid, i.e. a counted branch
//                     left without resolving.
//
// Build: make build/brleak_probe.out
// Run  : FROM=26810000 TO=26816000 build/brleak_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C(s)   tb_->system__DOT__chiron__DOT__core1__DOT__##s
#define SCH(s) C(scheduler__DOT__##s)

static const char *INJ[5] = {"waitForMTIP", "waitToInjectInterr",
                             "flushSpeculated", "waitToCommitBranch", "?"};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t FROM = env64("FROM", 26810000ULL);
  const uint64_t TO   = env64("TO",   26816500ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  unsigned p_bcnt = 0, p_occ = 0, p_inj = 0, p_rrd = 99, p_rwr = 99;
  unsigned p_bvalid[8] = {0,0,0,0,0,0,0,0};
  unsigned p_bisbr[8]  = {0,0,0,0,0,0,0,0};
  unsigned p_brob[8]   = {0,0,0,0,0,0,0,0};

  while (cyc < TO) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    unsigned qv[8], qb[8], qr[8], qi[8];
#define GET(i) do { qv[i] = (unsigned)SCH(queue_##i##_valid);                 \
                    qb[i] = (unsigned)SCH(queue_##i##_opcodeMeta_isBranch);   \
                    qr[i] = (unsigned)SCH(queue_##i##_robAddr);               \
                    qi[i] = (unsigned)SCH(queue_##i##_instruction); } while (0)
    GET(0); GET(1); GET(2); GET(3); GET(4); GET(5); GET(6); GET(7);
#undef GET

    unsigned occ = 0;
    for (int i = 0; i < 8; i++) occ += qv[i];
    const unsigned bcnt = (unsigned)C(branchCounter);
    const unsigned inj  = (unsigned)C(interruptInjectStatus);
    const unsigned bev  = (unsigned)C(branchEvals_valid);
    const unsigned bpas = (unsigned)C(branchEvals_passed);
    const unsigned biv  = (unsigned)C(branchInstruction_valid);

    if (cyc >= FROM) {
      // a counted branch leaving the scheduler with no resolution this cycle
      for (int i = 0; i < 8; i++) {
        if (p_bvalid[i] && !qv[i] && p_bisbr[i] && !bev) {
          printf("[cyc %llu] ** BRANCH-VANISHED from scheduler q[%d]: "
                 "insn=%08x rob=%u  (no branchEvals this cycle) "
                 "brCnt=%u occ=%u->%u\n",
                 (unsigned long long)cyc, i, qi[i], p_brob[i], bcnt, p_occ, occ);
        }
      }
      const unsigned rrd = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
      const unsigned rwr = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
      if (bcnt != p_bcnt || occ != p_occ || inj != p_inj || bev ||
          rrd != p_rrd || rwr != p_rwr) {
        printf("[cyc %llu] brCnt=%u%s occ=%u inj=%-18s "
               "brEval{v=%u pass=%u mask=%02x rob=%2u} brInstr{v=%u insn=%08x} "
               "robEmpty=%u robFull=%u robRd=%2u robWr=%2u "
               "schedAlloc=%u schedDeq=%u\n",
               (unsigned long long)cyc, bcnt,
               bcnt > p_bcnt ? "+" : (bcnt < p_bcnt ? "-" : " "), occ,
               INJ[inj < 4 ? inj : 4], bev, bpas,
               (unsigned)C(branchEvals_branchMask),
               (unsigned)C(branchEvals_robAddr), biv,
               (unsigned)C(branchInstruction_instruction),
               (unsigned)C(rob__DOT__fifo__DOT__emptyReg),
               (unsigned)C(rob__DOT__fifo__DOT__fullReg),
               (unsigned)C(rob__DOT__fifo__DOT__readPtr),
               (unsigned)C(rob__DOT__fifo__DOT__writePtr),
               (unsigned)C(scheduler_allocate_fired),
               (unsigned)SCH(dequeue));
      }
    }
    p_bcnt = bcnt; p_occ = occ; p_inj = inj;
    p_rrd = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
    p_rwr = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
    for (int i = 0; i < 8; i++) { p_bvalid[i] = qv[i]; p_bisbr[i] = qb[i]; p_brob[i] = qr[i]; }
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
