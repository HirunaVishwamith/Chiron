// injwedge_probe.cpp — mt-ipimux wedge: why can hart1 never inject its IPI?
//
// The wedge dump (ipitmr_wedge_probe on bins/mt-ipimux-q4.bin) says:
//   msip={0,1,0,0}                      hart1 has a pending software interrupt
//   core1 injSt=1 injSoft=1 canSoft=1   FSM is in waitToInjectInterr
//   core1 brCnt=1                       branchCounter never reaches 0
//   c1=0 commits over 8000 cycles       hart1 is completely frozen
//   core1 mem: all buffers empty, mExtRdy=1, extnMReq=0 — NOT a memory or
//                                       divider stall
//
// core.scala:1197 stalls fetch+decode for the whole time the FSM is in
// waitToInjectInterr with branchCounter != 0, and core.scala:1160-1183 only
// leaves that state via one of:
//   (a) branchCounter == 0  -> inject the fake system instruction, or
//   (b) branchCounter != 0 AND branchInstruction.valid AND the instruction in
//       the branch unit is a conditional branch (opcode 1100011)
//         -> flushSpeculated
// With the front end stalled, (b) can only be satisfied by a branch that is
// ALREADY inside the scheduler and still able to issue. So the question that
// decides the root cause is simply:
//
//   is that outstanding branch real, or is branchCounter a phantom?
//
// If the ROB and the scheduler queue are empty while branchCounter reads 1,
// the count is phantom: nothing will ever issue, branchEvals.valid can never
// fire, the counter can never decrement, and neither exit condition can ever
// be met -> permanent wedge. (core.scala:1102 computes the counter with
// `+& incr -& decr`, so a decrement with no matching increment underflows
// rather than saturating at 0.)
//
// If instead the scheduler still holds a branch whose rs1/rs2 are not ready,
// the count is real and the bug is a lost producer feeding it.
//
// The probe keeps a ring buffer of every branchCounter / FSM transition so the
// last increments and decrements before the freeze are visible, then dumps the
// full scheduler queue once hart1 has clearly stopped committing.
//
// Build: make build/injwedge_probe.out
// Run  : build/injwedge_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "sim/rtl/rtl_model.h"

#define C(s) tb_->system__DOT__chiron__DOT__core1__DOT__##s
#define SCH(s) C(scheduler__DOT__##s)

static const char *INJ[5] = {"waitForMTIP", "waitToInjectInterr",
                             "flushSpeculated", "waitToCommitBranch", "?"};

struct Ev {
  uint64_t cyc;
  unsigned bcnt, inj, bev, bpass, bmask, brob;
  unsigned biv, binsn;
  unsigned fired, robEmpty;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END   = env64("END", 28000000ULL);
  // once the FSM has sat in a non-idle state this long, call it wedged
  const uint64_t STUCK = env64("STUCK", 400000ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  const int RING = 64;
  Ev ring[RING];
  int rn = 0;
  memset(ring, 0, sizeof(ring));

  uint64_t cyc = 0;
  unsigned prev_bcnt = 0, prev_inj = 0;
  uint64_t inj_since = 0;
  bool reported = false;

  while (cyc < END && !reported) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const unsigned bcnt = (unsigned)C(branchCounter);
    const unsigned inj  = (unsigned)C(interruptInjectStatus);

    if (bcnt != prev_bcnt || inj != prev_inj) {
      Ev &e = ring[rn % RING]; rn++;
      e.cyc = cyc; e.bcnt = bcnt; e.inj = inj;
      e.bev   = (unsigned)C(branchEvals_valid);
      e.bpass = (unsigned)C(branchEvals_passed);
      e.bmask = (unsigned)C(branchEvals_branchMask);
      e.brob  = (unsigned)C(branchEvals_robAddr);
      e.biv   = (unsigned)C(branchInstruction_valid);
      e.binsn = (unsigned)C(branchInstruction_instruction);
      e.robEmpty = (unsigned)C(rob__DOT__fifo__DOT__emptyReg);
      prev_bcnt = bcnt; prev_inj = inj;
    }
    if (inj == 0) inj_since = cyc;

    if (inj != 0 && cyc - inj_since > STUCK) {
      reported = true;
      printf("\n===== hart1 wedged at cyc %llu =====\n", (unsigned long long)cyc);
      printf("interruptInjectStatus = %s (stuck since cyc %llu)\n",
             INJ[inj < 4 ? inj : 4], (unsigned long long)inj_since);
      printf("branchCounter         = %u\n", bcnt);
      printf("injectingSoftwareInt  = %u\n", (unsigned)C(injectingSoftwareInterrupt));
      printf("rob.fifo.empty        = %u\n", (unsigned)C(rob__DOT__fifo__DOT__emptyReg));
      printf("branchInstruction     = valid=%u insn=%08x rob=%u\n",
             (unsigned)C(branchInstruction_valid),
             (unsigned)C(branchInstruction_instruction),
             (unsigned)C(branchInstruction_robAddr));
      printf("branchEvals           = valid=%u passed=%u mask=%02x rob=%u\n",
             (unsigned)C(branchEvals_valid), (unsigned)C(branchEvals_passed),
             (unsigned)C(branchEvals_branchMask), (unsigned)C(branchEvals_robAddr));
      printf("lastBranchExecRob     = %u\n", (unsigned)C(lastBranchExecRob));

      printf("\n--- scheduler queue (is anything still able to issue?) ---\n");
#define Q(i)                                                                  \
      printf("  q[%d] valid=%u insn=%08x rob=%2u prfDest=%2u "                \
             "rs1{prf=%2u rdy=%u} rs2{prf=%2u rdy=%u} mask=%02x "             \
             "isBranch=%u isM=%u isMem=%u\n", i,                              \
             (unsigned)SCH(queue_##i##_valid),                                \
             (unsigned)SCH(queue_##i##_instruction),                          \
             (unsigned)SCH(queue_##i##_robAddr),                              \
             (unsigned)SCH(queue_##i##_prfDest),                              \
             (unsigned)SCH(queue_##i##_rs1_prfAddr),                          \
             (unsigned)SCH(queue_##i##_rs1_ready),                            \
             (unsigned)SCH(queue_##i##_rs2_prfAddr),                          \
             (unsigned)SCH(queue_##i##_rs2_ready),                            \
             (unsigned)SCH(queue_##i##_branchMask),                           \
             (unsigned)SCH(queue_##i##_opcodeMeta_isBranch),                  \
             (unsigned)SCH(queue_##i##_opcodeMeta_isM),                       \
             (unsigned)SCH(queue_##i##_opcodeMeta_isMemAccess))
      Q(0); Q(1); Q(2); Q(3); Q(4); Q(5); Q(6); Q(7);
#undef Q

      printf("\n--- last %d branchCounter / FSM transitions ---\n",
             rn < RING ? rn : RING);
      const int n = rn < RING ? rn : RING;
      const int start = rn < RING ? 0 : rn % RING;
      for (int k = 0; k < n; k++) {
        const Ev &e = ring[(start + k) % RING];
        printf("  [cyc %10llu] brCnt=%u inj=%-18s brEval{v=%u pass=%u mask=%02x rob=%u} "
               "brInstr{v=%u insn=%08x} robEmpty=%u\n",
               (unsigned long long)e.cyc, e.bcnt, INJ[e.inj < 4 ? e.inj : 4],
               e.bev, e.bpass, e.bmask, e.brob, e.biv, e.binsn, e.robEmpty);
      }
    }
  }
  if (!reported) printf("no wedge within %llu cycles\n", (unsigned long long)cyc);
  return 0;
}
