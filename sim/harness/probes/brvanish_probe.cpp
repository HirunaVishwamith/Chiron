// brvanish_probe.cpp — the wedged ROB head is a BRANCH that never resolved.
//                      Which resolution went missing, and what did it carry?
//
// robhead_probe settled what the mt-ipimux wedge actually is. hart1's ROB head
// is not a stalled load waiting on a functional unit:
//
//   HEAD [14] pc=0x80000540 insn=f90614e3 (bne a2,a6)  allocated @26814043
//             ready bit last changed @26814014, i.e. BEFORE this allocation
//             -> the current occupant was never completed
//
// A branch's ROB ready bit has exactly one writer: rob.scala:115-117,
//   results.writeports(N).valid := branch.valid
//   results.writeports(N).addr  := branch.robAddr
// driven from core.scala's branchEvals. So slot 14 never went ready means no
// branchEvals ever carried robAddr=14. Meanwhile robmodify_probe caught the
// other half of the story at cyc 26814119: a resolution DID fire with
// robAddr=12 while readPtr was already 14 — a resolution addressed to an entry
// that had retired three cycles earlier. And in the final dump slot 12 is one
// of only four slots holding rdy=1.
//
// Those two facts fit one hypothesis: the head branch's resolution was emitted
// with the WRONG robAddr. That single event explains everything downstream at
// once — slot 14 never goes ready (ROB jams full), slot 12 goes spuriously
// ready, and fifo.modify fires out-of-window (the rollback the Fifo.scala
// doModify guard now discards). The guard stops the pointer corruption but
// cannot put the ready bit back on the right slot, which is why it fixed a real
// defect without clearing this wedge.
//
// The competing hypothesis is that the head branch never issued at all and the
// robAddr=12 resolution belongs to a genuinely older branch (a stale/duplicate
// resolution). These are distinguishable: watch whether a scheduler entry
// carrying robAddr=14 is ever dequeued/issued.
//
// So this probe traces, over a window around the freeze:
//   * every branchEvals (valid/passed/robAddr/mask) and every branchInstruction
//     (valid/robAddr/instruction/pc) — the resolution and the stage feeding it,
//   * scheduler occupancy and any entry whose robAddr is the target slot,
//   * BRANCH-VANISHED: a scheduler entry with isBranch=1 going valid->invalid
//     on a cycle with no branchEvals (a counted branch leaving unresolved),
//   * ISSUED-TARGET / RESOLVED-TARGET markers for the slot under suspicion.
//
// PRE-EDGE sampling throughout (see robmodify_probe.cpp for why that matters).
//
// Build: make build/brvanish_probe.out
// Run  : TARGET=14 FROM=26813900 TO=26814400 build/brvanish_probe.out \
//            bins/mt-ipimux-q4.bin
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
  const uint64_t FROM   = env64("FROM", 26813900ULL);
  const uint64_t TO     = env64("TO",   26814400ULL);
  const unsigned TARGET = (unsigned)env64("TARGET", 14ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  unsigned p_qv[8] = {0}, p_qb[8] = {0}, p_qr[8] = {0};
  uint32_t p_qi[8] = {0};
  unsigned p_bcnt = 0, p_inj = 99, p_rd = 99, p_wr = 99;

  uint64_t nResolve = 0, nTargetResolve = 0, nTargetIssue = 0, nVanish = 0;
  uint64_t firstTargetIssue = 0;

  while (cyc < TO) {
    tb_->eval();

    unsigned qv[8], qb[8], qr[8]; uint32_t qi[8];
#define GET(i) do { qv[i] = (unsigned)SCH(queue_##i##_valid);               \
                    qb[i] = (unsigned)SCH(queue_##i##_opcodeMeta_isBranch); \
                    qr[i] = (unsigned)SCH(queue_##i##_robAddr);             \
                    qi[i] = (uint32_t)SCH(queue_##i##_instruction); } while (0)
    GET(0); GET(1); GET(2); GET(3); GET(4); GET(5); GET(6); GET(7);
#undef GET

    unsigned occ = 0; for (int i = 0; i < 8; i++) occ += qv[i];
    const unsigned bev  = (unsigned)C(branchEvals_valid);
    const unsigned bpas = (unsigned)C(branchEvals_passed);
    const unsigned brob = (unsigned)C(branchEvals_robAddr);
    const unsigned bmsk = (unsigned)C(branchEvals_branchMask);
    const unsigned biv  = (unsigned)C(branchInstruction_valid);
    const uint32_t bii  = (uint32_t)C(branchInstruction_instruction);
    const unsigned bcnt = (unsigned)C(branchCounter);
    const unsigned inj  = (unsigned)C(interruptInjectStatus);
    const unsigned rd   = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
    const unsigned wr   = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
    const unsigned deq  = (unsigned)SCH(dequeue);

    if (bev) { nResolve++; if (brob == TARGET) nTargetResolve++; }

    // Is the target slot's instruction sitting in / leaving the scheduler?
    int tgtSlot = -1;
    for (int i = 0; i < 8; i++) if (qv[i] && qr[i] == TARGET) tgtSlot = i;

    if (cyc >= FROM && cyc <= TO) {
      for (int i = 0; i < 8; i++) {
        if (p_qv[i] && !qv[i]) {
          const bool isTgt = (p_qr[i] == TARGET);
          if (p_qb[i] && !bev) {
            nVanish++;
            printf("[cyc %llu] ** BRANCH-VANISHED q[%d] insn=%08x rob=%u "
                   "(no branchEvals this cycle)%s\n",
                   (unsigned long long)cyc, i, p_qi[i], p_qr[i],
                   isTgt ? "   <<< THIS IS THE WEDGED HEAD" : "");
          } else if (isTgt) {
            if (!nTargetIssue) firstTargetIssue = cyc;
            nTargetIssue++;
            printf("[cyc %llu] ISSUED-TARGET q[%d] insn=%08x rob=%u "
                   "(branchEvals this cycle: v=%u rob=%u)\n",
                   (unsigned long long)cyc, i, p_qi[i], p_qr[i], bev, brob);
          }
        }
      }
      if (bev) {
        printf("[cyc %llu] RESOLVE v=1 pass=%u rob=%2u mask=%02x  "
               "brInstr{v=%u insn=%08x}  robRd=%2u robWr=%2u  brCnt=%u "
               "occ=%u inj=%s%s\n",
               (unsigned long long)cyc, bpas, brob, bmsk, biv, bii, rd, wr,
               bcnt, occ, INJ[inj < 4 ? inj : 4],
               brob == TARGET ? "   <<< TARGET RESOLVED" : "");
      } else if (bcnt != p_bcnt || inj != p_inj || rd != p_rd || wr != p_wr ||
                 tgtSlot >= 0) {
        printf("[cyc %llu] brCnt=%u occ=%u inj=%-18s robRd=%2u robWr=%2u "
               "deq=%u tgtInSched=%s\n",
               (unsigned long long)cyc, bcnt, occ, INJ[inj < 4 ? inj : 4],
               rd, wr, deq,
               tgtSlot >= 0 ? "yes" : "no");
      }
    }

    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    for (int i = 0; i < 8; i++) { p_qv[i]=qv[i]; p_qb[i]=qb[i]; p_qr[i]=qr[i]; p_qi[i]=qi[i]; }
    p_bcnt = bcnt; p_inj = inj; p_rd = rd; p_wr = wr;
  }

  printf("\n=== summary over %llu cycles (hart1, TARGET rob=%u) ===\n",
         (unsigned long long)cyc, TARGET);
  printf("  branch resolutions total      : %llu\n", (unsigned long long)nResolve);
  printf("  resolutions addressed to rob=%u: %llu\n", TARGET,
         (unsigned long long)nTargetResolve);
  printf("  scheduler issues of rob=%-2u    : %llu (first @%llu)\n", TARGET,
         (unsigned long long)nTargetIssue, (unsigned long long)firstTargetIssue);
  printf("  branch-vanished events        : %llu\n", (unsigned long long)nVanish);
  printf("\nReading: issues>0 with resolutions==0 => the branch DID execute and\n"
         "its resolution was emitted under the wrong robAddr. issues==0 =>\n"
         "the branch never reached a functional unit at all.\n");
  return 0;
}
