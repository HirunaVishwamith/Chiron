// lifetime_probe.cpp — follow the wrong-path load through every D-cache
// structure and find where its squash is lost.
//
// The chain established so far (all measured, none inferred):
//   1. prfwrite_probe : PRF[33] (a4's committed mapping) is overwritten at cyc
//      9067568 by write port w3 — the D-cache load return — carrying
//      insn 0x18063783 = `ld a5,384(a2)`, result 0x290.
//   2. the disassembly: that load lives at 0x488, PAST the loop exit of
//      `bne a4,a6,454` at 0x474, so it is fetched only on the wrong path and
//      squashed every iteration.
//   3. w3path_probe : the response arrived via the CACHE path (not the
//      ungated peripheral path) with branch.valid == 1.
//   4. mshr dump : the ACE MSHR entry for it (addr 0x80001280, prfDest 33) IS
//      correctly squashed — brValid=0, brMask=0 from cyc 9067399 onward — yet
//      the entry's own `valid` stays 1, so the transaction still completes
//      (fifo.scala:130 gates read.data.valid on the ENTRY's valid, not on
//      branch.valid).
//   5. squashmiss_probe : by the time it reaches cacheLookup.readBuffer at
//      9067567 it reads branch.valid=1, mask=0 — i.e. it looks NON-speculative
//      again, so cacheModule.scala:160's branch.valid gate passes it through.
//
// Between (4) and (5) the squash is destroyed. This probe prints every
// structure that holds the instruction, on every change, so the exact stage
// that resurrects branch.valid can be named rather than guessed. Branch
// resolutions are interleaved so mask arithmetic can be checked step by step.
//
// Build: make build/lifetime_probe.out
// Run  : build/lifetime_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(s)  tb_->system__DOT__chiron__DOT__core0__DOT__##s
#define MA(s)  tb_->system__DOT__chiron__DOT__core0__DOT__memAccess__DOT__##s
#define RS(s)  MA(requestScheduler__DOT__##s)
#define AR(s)  MA(arbiter__DOT__##s)
#define CL(s)  MA(cacheLookup__DOT__##s)
#define RU(s)  MA(replayUnit__DOT__requestWaitFIFO__DOT__##s)
#define AC(s)  MA(aceUnit__DOT__##s)

static uint64_t INSN = 0x18063783ULL;

// Remember the last (valid, branch.valid, mask) printed for each watched slot
// so only transitions are logged.
struct Prev { int v = -1, bv = -1, bm = -1; };

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t STOP = env64("STOP", 9067600ULL);
  const uint64_t FROM = env64("FROM", 9067200ULL);
  INSN = env64("INSN", INSN);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  static Prev prev[64];
  int nslot = 0;

  auto report = [&](int slot, const char *name, unsigned insn_match,
                    unsigned v, unsigned bv, unsigned bm, unsigned dest,
                    unsigned rob) {
    if (!insn_match) return;
    Prev &p = prev[slot];
    if ((int)v == p.v && (int)bv == p.bv && (int)bm == p.bm) return;
    p.v = v; p.bv = bv; p.bm = bm;
    printf("[cyc %llu]  %-34s valid=%u branch.valid=%u mask=%02x "
           "prfDest=%u rob=%u\n",
           (unsigned long long)cyc, name, v, bv, bm, dest, rob);
  };

  while (cyc < STOP) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < FROM) continue;

    if ((unsigned)C0(branchEvals_valid))
      printf("[cyc %llu] BRANCH %s mask=%x rob=%u nextPC=%llx\n",
             (unsigned long long)cyc,
             (unsigned)C0(branchEvals_passed) ? "PASS" : "FAIL",
             (unsigned)C0(branchEvals_branchMask),
             (unsigned)C0(branchEvals_robAddr),
             (unsigned long long)C0(branchEvals_nextPC));

    nslot = 0;
#define WATCH(NAME, INS, V, BV, BM, D, R) \
    report(nslot++, NAME, (unsigned)((uint64_t)(INS) == INSN), \
           (unsigned)(V), (unsigned)(BV), (unsigned)(BM), \
           (unsigned)(D), (unsigned)(R))

    WATCH("requestScheduler.requestOut", MA(requestScheduler_requestOut_core_instruction),
          MA(requestScheduler_requestOut_valid), MA(requestScheduler_requestOut_branch_valid),
          MA(requestScheduler_requestOut_branch_mask),
          MA(requestScheduler_requestOut_core_prfDest), MA(requestScheduler_requestOut_core_robAddr));

    WATCH("arbiter.speculativeBuffer", AR(speculativeBuffer_core_instruction),
          AR(speculativeBuffer_valid), AR(speculativeBuffer_branch_valid),
          AR(speculativeBuffer_branch_mask), 0, 0);
    WATCH("arbiter.inorderBuffer", AR(inorderBuffer_core_instruction),
          AR(inorderBuffer_valid), AR(inorderBuffer_branch_valid),
          AR(inorderBuffer_branch_mask), 0, 0);
    WATCH("arbiter.operationBuffer", AR(operationBuffer_core_instruction),
          AR(operationBuffer_valid), AR(operationBuffer_branch_valid),
          AR(operationBuffer_branch_mask), 0, 0);
    WATCH("arbiter.toCacheLookup", MA(arbiter_toCacheLookup_request_core_instruction),
          MA(arbiter_toCacheLookup_request_valid), MA(arbiter_toCacheLookup_request_branch_valid),
          MA(arbiter_toCacheLookup_request_branch_mask), 0, 0);

    WATCH("cacheLookup.readBuffer", CL(readBuffer_core_instruction),
          CL(readBuffer_valid), CL(readBuffer_branch_valid), CL(readBuffer_branch_mask),
          CL(readBuffer_core_prfDest), CL(readBuffer_core_robAddr));
    WATCH("cacheLookup.replayBuffer", CL(replayBuffer_core_instruction),
          CL(replayBuffer_valid), CL(replayBuffer_branch_valid), CL(replayBuffer_branch_mask),
          CL(replayBuffer_core_prfDest), CL(replayBuffer_core_robAddr));
    WATCH("cacheLookup.lastSpecMissRecord", CL(lastSpeculativeMissRecordRegister_core_instruction),
          CL(lastSpeculativeMissRecordRegister_valid),
          CL(lastSpeculativeMissRecordRegister_branch_valid),
          CL(lastSpeculativeMissRecordRegister_branch_mask), 0, 0);
    WATCH("cacheLookup.lastInorderMissRecord", CL(lastInorderMissRecordRegister_core_instruction),
          CL(lastInorderMissRecordRegister_valid),
          CL(lastInorderMissRecordRegister_branch_valid),
          CL(lastInorderMissRecordRegister_branch_mask), 0, 0);

    WATCH("aceUnit.readBuffer", AC(readBuffer_core_instruction),
          AC(readBuffer_valid), AC(readBuffer_branch_valid), AC(readBuffer_branch_mask),
          AC(readBuffer_core_prfDest), AC(readBuffer_core_robAddr));
    WATCH("aceUnit.responseBuffer", AC(responseBuffer_core_instruction),
          AC(responseBuffer_valid), AC(responseBuffer_branch_valid),
          AC(responseBuffer_branch_mask),
          AC(responseBuffer_core_prfDest), AC(responseBuffer_core_robAddr));

    // replayUnit FIFO slots — the stage between the ACE refill and the replay.
#define RUSLOT(i) \
    WATCH("replayUnit.requestWaitFIFO[" #i "]", RU(memReg_##i##_core_instruction), \
          RU(memReg_##i##_valid), RU(memReg_##i##_branch_valid), \
          RU(memReg_##i##_branch_mask), RU(memReg_##i##_core_prfDest), \
          RU(memReg_##i##_core_robAddr))
    RUSLOT(0);  RUSLOT(1);  RUSLOT(2);  RUSLOT(3);
    RUSLOT(4);  RUSLOT(5);  RUSLOT(6);  RUSLOT(7);
    RUSLOT(8);  RUSLOT(9);  RUSLOT(10); RUSLOT(11);
    RUSLOT(12); RUSLOT(13); RUSLOT(14); RUSLOT(15);
#undef RUSLOT
#undef WATCH
  }
  printf("\ndone cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
