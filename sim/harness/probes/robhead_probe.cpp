// robhead_probe.cpp — which instruction is the wedged ROB head, and who owes
// it a completion?
//
// State established so far (robmodify_probe + the Fifo.scala doModify guard):
// mt-ipimux wedges hart1 with the ROB permanently FULL. The out-of-window
// rollback that used to GROW the fifo is fixed and gone; the fifo now fills
// legitimately — readPtr parks at 14 with commit.ready = 0 while normal
// enqueues take occupancy 13 -> 16. Everything downstream (decode stall,
// scheduler drain, branchCounter stuck at 1, injection FSM parked in
// waitToInjectInterr with fetch frozen, IPI never taken) follows from that
// one fact, so the remaining question is narrow:
//
//     why does the entry at rob=readPtr never get its ready bit?
//
// rob.scala:84  commit.ready = results.deq.bits(0) | is_fence | isStore
// and results.deq.bits(0) is written only by
//   * an execPort  (rob.scala:120-126, one per functional unit), or
//   * the branch port (rob.scala:115-117, on branch.valid, pass or fail).
// So a head that never goes ready is an instruction whose functional unit
// never reported back. Naming the instruction names the unit.
//
// This probe therefore:
//   1. decodes the head entry out of rob.fifo.memReg  (PC | insn | prfDest,
//      rob.scala:23) and its ready bit out of rob.results.memReg,
//   2. records, for every slot, the cycle it was last allocated and the
//      cycle its ready bit last changed — so at the wedge we can say whether
//      the head was NEVER made ready or was made ready and then wiped by a
//      reallocation/rollback,
//   3. classifies the head instruction's opcode, which points at the unit.
//
// Sampling is PRE-EDGE (after eval(), before toggling clock) throughout, for
// the reason robmodify_probe.cpp documents at length: post-edge pointer reads
// produced a confidently wrong inference once already.
//
// Build: make build/robhead_probe.out
// Run  : TO=27200000 build/robhead_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C(s) tb_->system__DOT__chiron__DOT__core1__DOT__##s

// Extract [lo +: len] (len <= 64) from a Verilator WData array.
static uint64_t wbits(const uint32_t *w, int lo, int len) {
  uint64_t v = 0;
  for (int i = 0; i < len; i++) {
    const int b = lo + i;
    v |= (uint64_t)((w[b >> 5] >> (b & 31)) & 1u) << i;
  }
  return v;
}

static const char *opname(uint32_t insn) {
  switch (insn & 0x7f) {
    case 0x03: return "LOAD";
    case 0x23: return "STORE";
    case 0x2f: return "AMO/LR/SC";
    case 0x63: return "BRANCH";
    case 0x67: return "JALR";
    case 0x6f: return "JAL";
    case 0x73: return "SYSTEM/CSR";
    case 0x0f: return "FENCE";
    case 0x33: return (insn >> 25) == 1 ? "OP(M-ext)" : "OP";
    case 0x3b: return (insn >> 25) == 1 ? "OP-32(M-ext)" : "OP-32";
    case 0x13: return "OP-IMM";
    case 0x1b: return "OP-IMM-32";
    case 0x37: return "LUI";
    case 0x17: return "AUIPC";
    default:   return "?";
  }
}

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t TO       = env64("TO", 27200000ULL);
  const uint64_t STUCK_N  = env64("STUCK", 200000ULL);  // head unchanged this long => wedged

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  uint64_t allocCyc[16] = {0};      // cycle slot i was last written by an enqueue
  uint64_t allocInsn[16] = {0};
  uint64_t allocPC[16] = {0};
  uint64_t readyCyc[16] = {0};      // cycle slot i's ready bit last changed
  unsigned readyPrev[16] = {0};
  uint64_t readySet[16] = {0}, readyClr[16] = {0};

  unsigned lastRd = 99;
  uint64_t rdSince = 0;
  bool reported = false;

  while (cyc < TO) {
    tb_->eval();

    const unsigned rd = (unsigned)C(rob__DOT__fifo__DOT__readPtr);
    const unsigned wr = (unsigned)C(rob__DOT__fifo__DOT__writePtr);
    const unsigned ful = (unsigned)C(rob__DOT__fifo__DOT__fullReg);
    const unsigned emp = (unsigned)C(rob__DOT__fifo__DOT__emptyReg);
    const unsigned allocF = (unsigned)C(rob_allocate_fired);

    // ready bits, all 16 slots
    unsigned rdy[16];
    for (int i = 0; i < 16; i++)
      rdy[i] = (unsigned)(C(rob__DOT__results__DOT__memReg)[i][0] & 1u);

    if (rd != lastRd) { lastRd = rd; rdSince = cyc; }

    if (allocF) {
      const uint32_t *e = (const uint32_t *)&C(rob__DOT__fifo_io_enq_bits);
      allocCyc[wr]  = cyc;
      allocPC[wr]   = wbits(e, 38, 64);
      allocInsn[wr] = wbits(e, 6, 32);
    }
    for (int i = 0; i < 16; i++) {
      if (rdy[i] != readyPrev[i]) {
        readyCyc[i] = cyc;
        if (rdy[i]) readySet[i]++; else readyClr[i]++;
        readyPrev[i] = rdy[i];
      }
    }

    // ---- wedge detected: head has not advanced for STUCK_N cycles ----
    if (!reported && ful && (cyc - rdSince) > STUCK_N) {
      reported = true;
      printf("\n=== ROB WEDGE (hart1) at cyc %llu — head parked since %llu ===\n",
             (unsigned long long)cyc, (unsigned long long)rdSince);
      printf("readPtr=%u writePtr=%u full=%u empty=%u  commit.ready=%u "
             "headValid=%u allocate.ready=%u\n",
             rd, wr, ful, emp, (unsigned)C(rob_commit_ready),
             (unsigned)C(rob_headValid), (unsigned)C(rob_allocate_ready));
      printf("\n slot  rdy  allocCyc     allocPC            insn      opcode         "
             "readyCyc     set clr\n");
      for (int k = 0; k < 16; k++) {
        const int i = (rd + k) & 15;
        const uint32_t *m = (const uint32_t *)&C(rob__DOT__fifo__DOT__memReg)[i];
        const uint64_t pc   = wbits(m, 38, 64);
        const uint32_t insn = (uint32_t)wbits(m, 6, 32);
        printf("%s[%2d]  %u   %10llu  0x%016llx  %08x  %-14s %10llu  %3llu %3llu\n",
               k == 0 ? "HEAD " : "     ", i, rdy[i],
               (unsigned long long)allocCyc[i], (unsigned long long)pc, insn,
               opname(insn), (unsigned long long)readyCyc[i],
               (unsigned long long)readySet[i], (unsigned long long)readyClr[i]);
        (void)pc;
      }
      // The head's own entry, spelled out.
      const uint32_t *m = (const uint32_t *)&C(rob__DOT__fifo__DOT__memReg)[rd];
      const uint32_t insn = (uint32_t)wbits(m, 6, 32);
      printf("\nHEAD  pc=0x%016llx  insn=%08x (%s)  prfDest=x%llu\n",
             (unsigned long long)wbits(m, 38, 64), insn, opname(insn),
             (unsigned long long)wbits(m, 0, 6));
      printf("HEAD  allocated @%llu ; ready bit last changed @%llu "
             "(set %llu times, cleared %llu times)\n",
             (unsigned long long)allocCyc[rd],
             (unsigned long long)readyCyc[rd],
             (unsigned long long)readySet[rd],
             (unsigned long long)readyClr[rd]);
      if (readyCyc[rd] == 0 && !rdy[rd])
        printf("HEAD  ready bit NEVER asserted since reset — its unit never "
               "reported back.\n");
      else if (!rdy[rd] && readyCyc[rd] > allocCyc[rd])
        printf("HEAD  ready bit was CLEARED after this allocation — a "
               "reallocation/rollback wiped a completed result.\n");
      else if (!rdy[rd])
        printf("HEAD  ready bit last changed BEFORE this allocation — the "
               "current occupant was never completed.\n");
      fflush(stdout);
      break;
    }

    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
  }

  if (!reported)
    printf("no wedge detected within %llu cycles\n", (unsigned long long)cyc);
  return 0;
}
