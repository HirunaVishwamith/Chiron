// linux_storegate_probe.cpp — why does core2's D-cache write-commit port never
// reopen during the Linux boot?
//
// 2026-08-22. With the branchRes off-by-one fixed (core.scala branchResSnap),
// the kernel hart (core2 — cores 0/1/3 are parked secondaries) stops retiring
// at ~2.85M cycles inside __memset (pc 0x8053df3c, `sd a1,176(t0)`).
// Its perf counters say the ROB head is READY and 100% of the blocked cycles
// are the store gate: rob_ready_blocked == rnr_store_gate == 7,871,289.
//
// That gate is cacheLookupUnit.scala:315
//     writeInstructionCommit.ready := writeCommitInstructionBuffer
// which is set only when a store pass HITS. So core2's store is stuck forever
// in the miss -> replay path. This probe boots from reset, waits for the gate
// to stay shut, then dumps every buffer on that path (cacheLookup miss records
// / replay buffer, the arbiter's operation & inorder buffers, the ACE unit's
// FSMs and all 16 MSHR entries) plus a short per-cycle trace afterwards so a
// live-but-looping path is distinguishable from a dead one.
//
// Build: make build/linux_storegate_probe.out
// Run  : HART=2 STALL=300000 ./build/linux_storegate_probe.out
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "sim/rtl/rtl_model.h"

#define CL(n, s)  tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__##s
#define AR(n, s)  tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__arbiter__DOT__##s
#define AC(n, s)  tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__aceUnit__DOT__##s
#define MS(n, s)  tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__aceUnit__DOT__ACEMSHR__DOT__##s
#define RB(n, s)  tb_->system__DOT__chiron__DOT__core##n##__DOT__rob__DOT__##s

// The probe only ever watches one hart; a switch per signal keeps the macro
// soup out of the reporting code.
#define PICK(EXPR)                                                            \
  [&](int n) -> unsigned long long {                                          \
    switch (n) { case 0: return (unsigned long long)(EXPR(0));                 \
                 case 1: return (unsigned long long)(EXPR(1));                 \
                 case 2: return (unsigned long long)(EXPR(2));                 \
                 default: return (unsigned long long)(EXPR(3)); } }

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";
  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const int      H     = (int)env64("HART", 2);
  const uint64_t END   = env64("END", 12000000ULL);
  const uint64_t STALL = env64("STALL", 300000ULL);
  const uint64_t TRACE = env64("TRACE", 120ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

#define S_gate(n)   CL(n, writeCommitInstructionBuffer)
#define S_opval(n)  CL(n, operationValid)
#define S_rtype(n)  CL(n, requestType)
#define S_rbv(n)    CL(n, readBuffer_valid)
#define S_rba(n)    CL(n, readBuffer_address)
#define S_rbbv(n)   CL(n, readBuffer_branch_valid)
#define S_rbi(n)    CL(n, readBuffer_core_instruction)
#define S_rbr(n)    CL(n, readBuffer_core_robAddr)
#define S_rbw(n)    CL(n, readBuffer_writeData_valid)
#define S_miv(n)    CL(n, lastInorderMissRecordRegister_valid)
#define S_mia(n)    CL(n, lastInorderMissRecordRegister_address)
#define S_mibv(n)   CL(n, lastInorderMissRecordRegister_branch_valid)
#define S_mii(n)    CL(n, lastInorderMissRecordRegister_core_instruction)
#define S_mir(n)    CL(n, lastInorderMissRecordRegister_core_robAddr)
#define S_msv(n)    CL(n, lastSpeculativeMissRecordRegister_valid)
#define S_msa(n)    CL(n, lastSpeculativeMissRecordRegister_address)
#define S_msbv(n)   CL(n, lastSpeculativeMissRecordRegister_branch_valid)
#define S_msi(n)    CL(n, lastSpeculativeMissRecordRegister_core_instruction)
#define S_msr(n)    CL(n, lastSpeculativeMissRecordRegister_core_robAddr)
#define S_rpv(n)    CL(n, replayBuffer_valid)
#define S_rpa(n)    CL(n, replayBuffer_address)
#define S_rpbv(n)   CL(n, replayBuffer_branch_valid)
#define S_rpi(n)    CL(n, replayBuffer_core_instruction)
#define S_rpr(n)    CL(n, replayBuffer_core_robAddr)
#define S_torpl(n)  CL(n, toReplayValidWire)
#define S_tomi(n)   CL(n, toLastInorderMissRecordRegisterWire)
#define S_dmiss(n)  CL(n, isDataMissWire)
#define S_pmiss(n)  CL(n, isPermissionMiss)
#define S_stale(n)  CL(n, isStaleUpgradeReplay)
#define S_wbv(n)    CL(n, writeBackBuffer_valid)
#define S_wwbv(n)   CL(n, walkerWriteBackBuffer_valid)
#define S_flush(n)  CL(n, flushState)
#define S_rgb(n)    CL(n, reservationGuardBlocked)
#define S_rgc(n)    CL(n, reservationGuardCnt)
#define S_ostate(n) AR(n, operationState)
#define S_abusy(n)  AR(n, atomicBusyState)
#define S_obv(n)    AR(n, operationBuffer_valid)
#define S_oba(n)    AR(n, operationBuffer_address)
#define S_obbv(n)   AR(n, operationBuffer_branch_valid)
#define S_obi(n)    AR(n, operationBuffer_core_instruction)
#define S_obr(n)    AR(n, operationBuffer_core_robAddr)
#define S_ibv(n)    AR(n, inorderBuffer_valid)
#define S_iba(n)    AR(n, inorderBuffer_address)
#define S_ibbv(n)   AR(n, inorderBuffer_branch_valid)
#define S_ibi(n)    AR(n, inorderBuffer_core_instruction)
#define S_ibr(n)    AR(n, inorderBuffer_core_robAddr)
#define S_sbv(n)    AR(n, speculativeBuffer_valid)
#define S_sba(n)    AR(n, speculativeBuffer_address)
#define S_sbbv(n)   AR(n, speculativeBuffer_branch_valid)
#define S_areq(n)   AC(n, readACERequestState)
#define S_ares(n)   AC(n, readACEResponseState)
#define S_awr(n)    AC(n, writeACEState)
#define S_acoh(n)   AC(n, coherentAXIState)
#define S_arbv(n)   AC(n, readBuffer_valid)
#define S_arba(n)   AC(n, readBuffer_address)
#define S_arbbv(n)  AC(n, readBuffer_branch_valid)
#define S_arspv(n)  AC(n, responseBuffer_valid)
#define S_arspa(n)  AC(n, responseBuffer_address)
#define S_arspbv(n) AC(n, responseBuffer_branch_valid)
#define S_awbv(n)   AC(n, writeBuffer_valid)
#define S_awba(n)   AC(n, writeBuffer_address)
#define S_acrv(n)   AC(n, coherencyRequestBuffer_valid)
#define S_acra(n)   AC(n, coherencyRequestBuffer_address)
#define S_msempty(n) MS(n, emptyReg)
#define S_msfull(n)  MS(n, fullReg)
#define S_fifodq(n)  RB(n, fifo_io_deq_valid)
#define S_resdq(n)   RB(n, results_io_deq_valid)
#define S_frp(n)     RB(n, fifo__DOT__readPtr)
#define S_fwp(n)     RB(n, fifo__DOT__writePtr)
#define S_rrp(n)     RB(n, results__DOT__readPtr)
#define S_rwp(n)     RB(n, results__DOT__writePtr)
#define S_mrv(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__memoryRequest_valid
#define S_mra(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__memoryRequest_address
#define S_mri(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__memoryRequest_instruction
#define S_canall(n)  tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess_canAllocate
#define S_cfired(n)  tb_->system__DOT__chiron__DOT__core##n##__DOT__rob_commit_fired
#define S_cready(n)  tb_->system__DOT__chiron__DOT__core##n##__DOT__rob_commit_ready
#define S_hhd(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__headHasData
#define S_fe(n)      RB(n, fifo__DOT__emptyReg)
#define S_ff(n)      RB(n, fifo__DOT__fullReg)
#define S_fm(n)      RB(n, fifo__DOT__doModify)
#define S_re(n)      RB(n, results__DOT__emptyReg)
#define S_rf(n)      RB(n, results__DOT__fullReg)
#define S_rm(n)      RB(n, results__DOT__doModify)
#define S_af(n)      tb_->system__DOT__chiron__DOT__core##n##__DOT__rob_allocate_fired
#define S_ar(n)      tb_->system__DOT__chiron__DOT__core##n##__DOT__rob_allocate_ready
#define S_bev(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__branchEvals_valid
#define S_bep(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__branchEvals_passed
#define S_ber(n)     tb_->system__DOT__chiron__DOT__core##n##__DOT__branchEvals_robAddr

  auto gate = PICK(S_gate); auto opval = PICK(S_opval); auto rtype = PICK(S_rtype);
  auto rbv = PICK(S_rbv); auto rba = PICK(S_rba); auto rbbv = PICK(S_rbbv);
  auto rbi = PICK(S_rbi); auto rbr = PICK(S_rbr); auto rbw = PICK(S_rbw);
  auto miv = PICK(S_miv); auto mia = PICK(S_mia); auto mibv = PICK(S_mibv);
  auto mii = PICK(S_mii); auto mir = PICK(S_mir);
  auto msv = PICK(S_msv); auto msa = PICK(S_msa); auto msbv = PICK(S_msbv);
  auto msi = PICK(S_msi); auto msr = PICK(S_msr);
  auto rpv = PICK(S_rpv); auto rpa = PICK(S_rpa); auto rpbv = PICK(S_rpbv);
  auto rpi = PICK(S_rpi); auto rpr = PICK(S_rpr);
  auto torpl = PICK(S_torpl); auto tomi = PICK(S_tomi);
  auto dmiss = PICK(S_dmiss); auto pmiss = PICK(S_pmiss); auto stale = PICK(S_stale);
  auto wbv = PICK(S_wbv); auto wwbv = PICK(S_wwbv); auto flushs = PICK(S_flush);
  auto rgb = PICK(S_rgb); auto rgc = PICK(S_rgc);
  auto ostate = PICK(S_ostate); auto abusy = PICK(S_abusy);
  auto obv = PICK(S_obv); auto oba = PICK(S_oba); auto obbv = PICK(S_obbv);
  auto obi = PICK(S_obi); auto obr = PICK(S_obr);
  auto ibv = PICK(S_ibv); auto iba = PICK(S_iba); auto ibbv = PICK(S_ibbv);
  auto ibi = PICK(S_ibi); auto ibr = PICK(S_ibr);
  auto sbv = PICK(S_sbv); auto sba = PICK(S_sba); auto sbbv = PICK(S_sbbv);
  auto areq = PICK(S_areq); auto ares = PICK(S_ares);
  auto awr = PICK(S_awr); auto acoh = PICK(S_acoh);
  auto arbv = PICK(S_arbv); auto arba = PICK(S_arba); auto arbbv = PICK(S_arbbv);
  auto arspv = PICK(S_arspv); auto arspa = PICK(S_arspa); auto arspbv = PICK(S_arspbv);
  auto awbv = PICK(S_awbv); auto awba = PICK(S_awba);
  auto acrv = PICK(S_acrv); auto acra = PICK(S_acra);
  auto msempty = PICK(S_msempty); auto msfull = PICK(S_msfull);
  auto fifodq = PICK(S_fifodq); auto resdq = PICK(S_resdq);
  auto frp = PICK(S_frp); auto fwp = PICK(S_fwp);
  auto rrp = PICK(S_rrp); auto rwp = PICK(S_rwp);
  auto mrv = PICK(S_mrv); auto mra = PICK(S_mra); auto mri = PICK(S_mri);
  auto canall = PICK(S_canall); auto cfired = PICK(S_cfired);
  auto fe = PICK(S_fe); auto ff = PICK(S_ff); auto fm = PICK(S_fm);
  auto re = PICK(S_re); auto rf = PICK(S_rf); auto rm = PICK(S_rm);
  auto af = PICK(S_af); auto ar = PICK(S_ar);
  auto bev = PICK(S_bev); auto bep = PICK(S_bep); auto ber = PICK(S_ber);
  auto cready = PICK(S_cready); auto hhd = PICK(S_hhd);

  // ROB fifo entry: {pc[63:0], instruction[31:0], prfDest[5:0]} packed into 102b.
  auto rob_entry = [&](int n, unsigned idx, uint64_t *pc, uint32_t *insn) {
    const uint32_t *w = nullptr;
    switch (n) {
      case 0: w = tb_->system__DOT__chiron__DOT__core0__DOT__rob__DOT__fifo__DOT__memReg[idx]; break;
      case 1: w = tb_->system__DOT__chiron__DOT__core1__DOT__rob__DOT__fifo__DOT__memReg[idx]; break;
      case 2: w = tb_->system__DOT__chiron__DOT__core2__DOT__rob__DOT__fifo__DOT__memReg[idx]; break;
      default: w = tb_->system__DOT__chiron__DOT__core3__DOT__rob__DOT__fifo__DOT__memReg[idx]; break;
    }
    auto bits = [&](unsigned lo, unsigned len) -> uint64_t {
      uint64_t v = 0;
      for (unsigned i = 0; i < len; ++i) {
        const unsigned b = lo + i;
        v |= (uint64_t)((w[b >> 5] >> (b & 31)) & 1u) << i;
      }
      return v;
    };
    *insn = (uint32_t)bits(6, 32);
    *pc   = bits(38, 64);
  };

  // Ring of the store-dispatch path, so the last store to reach the arbiter
  // before the freeze is visible even though everything is idle at the dump.
  struct Ev { uint64_t cyc, mra, oba, iba; uint32_t mri, obi, ibi;
              uint8_t mrv, ost, obv, ibv, gate, cf, ca, hd; };
  const size_t RING = 8192;
  std::vector<Ev> ring(RING);
  size_t rn = 0;
  // Second ring: ROB pointer + rollback history. A permanently "full" ROB with
  // an idle back end is the robFifo empty/full desync signature, so every
  // pointer move, commit, allocate and doModify is logged.
  struct Rv { uint64_t cyc; uint8_t frp, fwp, rrp, rwp, fe, ff, fm, re, rf, rm,
                            cf, af, ar, bev, bep; uint8_t ber; };
  std::vector<Rv> rring(RING);
  size_t rrn = 0;
  uint64_t prev_key = ~0ULL;

  // MSHR entries are 16 separate flattened structs; index them by hand.
#define MSHR_ROW(n, i)                                                        \
  std::fprintf(stderr,                                                        \
    "    mshr[%2d] v=%llu addr=0x%09llx brV=%llu rob=%2llu wr=%llu insn=%08llx\n", i, \
    (unsigned long long)MS(n, memReg_##i##_valid),                            \
    (unsigned long long)MS(n, memReg_##i##_address),                          \
    (unsigned long long)MS(n, memReg_##i##_branch_valid),                     \
    (unsigned long long)MS(n, memReg_##i##_core_robAddr),                     \
    (unsigned long long)MS(n, memReg_##i##_writeData_valid),                  \
    (unsigned long long)MS(n, memReg_##i##_core_instruction));
  auto dump_mshr = [&](int n) {
    switch (n) {
#define MSHR_ALL(N) case N:                                                   \
      MSHR_ROW(N,0)  MSHR_ROW(N,1)  MSHR_ROW(N,2)  MSHR_ROW(N,3)              \
      MSHR_ROW(N,4)  MSHR_ROW(N,5)  MSHR_ROW(N,6)  MSHR_ROW(N,7)              \
      MSHR_ROW(N,8)  MSHR_ROW(N,9)  MSHR_ROW(N,10) MSHR_ROW(N,11)             \
      MSHR_ROW(N,12) MSHR_ROW(N,13) MSHR_ROW(N,14) MSHR_ROW(N,15) break;
      MSHR_ALL(0) MSHR_ALL(1) MSHR_ALL(2) MSHR_ALL(3)
    }
  };


  // Scheduler queue (8 entries): is the head store still waiting on an
  // operand, or did it vanish from the issue window entirely?
#define SCH(n, i, f) tb_->system__DOT__chiron__DOT__core##n##__DOT__scheduler__DOT__queue_##i##_##f
#define SCH_ROW(n, i)                                                          \
  std::fprintf(stderr,                                                         \
    "    sched[%d] v=%llu rob=%2llu insn=%08llx mask=%llx rs1(p%llu %s)"        \
    " rs2(p%llu %s) mem=%llu br=%llu\n", i,                                    \
    (unsigned long long)SCH(n, i, valid),                                      \
    (unsigned long long)SCH(n, i, robAddr),                                    \
    (unsigned long long)SCH(n, i, instruction),                                \
    (unsigned long long)SCH(n, i, branchMask),                                 \
    (unsigned long long)SCH(n, i, rs1_prfAddr),                                \
    SCH(n, i, rs1_ready) ? "rdy" : "WAIT",                                     \
    (unsigned long long)SCH(n, i, rs2_prfAddr),                                \
    SCH(n, i, rs2_ready) ? "rdy" : "WAIT",                                     \
    (unsigned long long)SCH(n, i, opcodeMeta_isMemAccess),                     \
    (unsigned long long)SCH(n, i, opcodeMeta_isBranch));
  auto dump_sched = [&](int n) {
    switch (n) {
#define SCH_ALL(N) case N:                                                     \
      SCH_ROW(N,0) SCH_ROW(N,1) SCH_ROW(N,2) SCH_ROW(N,3)                      \
      SCH_ROW(N,4) SCH_ROW(N,5) SCH_ROW(N,6) SCH_ROW(N,7) break;
      SCH_ALL(0) SCH_ALL(1) SCH_ALL(2) SCH_ALL(3)
    }
  };


  // The D-cache requestScheduler's inorder queue only dequeues an entry once
  // its branchMask has cleared (inorderBranchResolved) or the entry was killed
  // (inorderBranchInvalidated). A leaked branch tag parks the head store here
  // forever: the ROB waits for the write-commit gate, the gate waits for a
  // cache write pass, and the write pass waits for a mask bit that never drops.
#define RQ(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__requestScheduler__DOT__##s
#define IQ_ROW(n, i)                                                           \
  std::fprintf(stderr,                                                         \
    "    inQ[%2d] v=%llu brV=%llu mask=%llx addr=0x%09llx rob=%2llu"            \
    " insn=%08llx\n", i,                                                       \
    (unsigned long long)RQ(n, inorderQueue__DOT__memReg_##i##_valid),           \
    (unsigned long long)RQ(n, inorderQueue__DOT__memReg_##i##_branch_valid),    \
    (unsigned long long)RQ(n, inorderQueue__DOT__memReg_##i##_branch_mask),     \
    (unsigned long long)RQ(n, inorderQueue__DOT__memReg_##i##_address),         \
    (unsigned long long)RQ(n, inorderQueue__DOT__memReg_##i##_core_robAddr),    \
    (unsigned long long)RQ(n, inorderQueue__DOT__memReg_##i##_core_instruction));
#define SQ_ROW(n, i)                                                           \
  std::fprintf(stderr,                                                         \
    "    spQ[%2d] v=%llu brV=%llu mask=%llx addr=0x%09llx rob=%2llu"            \
    " insn=%08llx\n", i,                                                       \
    (unsigned long long)RQ(n, speculativeQueue__DOT__memReg_##i##_valid),       \
    (unsigned long long)RQ(n, speculativeQueue__DOT__memReg_##i##_branch_valid),\
    (unsigned long long)RQ(n, speculativeQueue__DOT__memReg_##i##_branch_mask), \
    (unsigned long long)RQ(n, speculativeQueue__DOT__memReg_##i##_address),     \
    (unsigned long long)RQ(n, speculativeQueue__DOT__memReg_##i##_core_robAddr),\
    (unsigned long long)RQ(n, speculativeQueue__DOT__memReg_##i##_core_instruction));
  auto dump_rq = [&](int n) {
    switch (n) {
#define RQ_ALL(N) case N:                                                      \
      std::fprintf(stderr,                                                     \
        "  REQSCHED inQ(rd=%llu wr=%llu empty=%llu full=%llu)"                 \
        " spQ(rd=%llu wr=%llu empty=%llu full=%llu)"                           \
        " inResolved=%llu inInvalidated=%llu\n",                               \
        (unsigned long long)RQ(N, inorderQueue__DOT__readPtr),                 \
        (unsigned long long)RQ(N, inorderQueue__DOT__writePtr),                \
        (unsigned long long)RQ(N, inorderQueue__DOT__emptyReg),                \
        (unsigned long long)RQ(N, inorderQueue__DOT__fullReg),                 \
        (unsigned long long)RQ(N, speculativeQueue__DOT__readPtr),             \
        (unsigned long long)RQ(N, speculativeQueue__DOT__writePtr),            \
        (unsigned long long)RQ(N, speculativeQueue__DOT__emptyReg),            \
        (unsigned long long)RQ(N, speculativeQueue__DOT__fullReg),             \
        (unsigned long long)RQ(N, inorderBranchResolved),                      \
        (unsigned long long)RQ(N, inorderBranchInvalidated));                  \
      IQ_ROW(N,0)  IQ_ROW(N,1)  IQ_ROW(N,2)  IQ_ROW(N,3)                       \
      IQ_ROW(N,4)  IQ_ROW(N,5)  IQ_ROW(N,6)  IQ_ROW(N,7)                       \
      IQ_ROW(N,8)  IQ_ROW(N,9)  IQ_ROW(N,10) IQ_ROW(N,11)                      \
      IQ_ROW(N,12) IQ_ROW(N,13) IQ_ROW(N,14) IQ_ROW(N,15)                      \
      SQ_ROW(N,0)  SQ_ROW(N,1)  SQ_ROW(N,2)  SQ_ROW(N,3)                       \
      SQ_ROW(N,4)  SQ_ROW(N,5)  SQ_ROW(N,6)  SQ_ROW(N,7)                       \
      SQ_ROW(N,8)  SQ_ROW(N,9)  SQ_ROW(N,10) SQ_ROW(N,11)                      \
      SQ_ROW(N,12) SQ_ROW(N,13) SQ_ROW(N,14) SQ_ROW(N,15) break;
      RQ_ALL(0) RQ_ALL(1) RQ_ALL(2) RQ_ALL(3)
    }
  };

  auto snapshot = [&](uint64_t cyc, const char *why) {
    std::fprintf(stderr,
      "\n==== hart%d %s @ cycle %llu  pc=0x%llx ====\n", H, why,
      (unsigned long long)cyc, (unsigned long long)bench.core_pc(H));
    std::fprintf(stderr,
      "  ROB   fifo(deqV=%llu rd=%llu wr=%llu empty=%llu full=%llu mod=%llu)"
      " results(deqV=%llu rd=%llu wr=%llu empty=%llu full=%llu mod=%llu)"
      " allocReady=%llu\n",
      fifodq(H), frp(H), fwp(H), fe(H), ff(H), fm(H),
      resdq(H), rrp(H), rwp(H), re(H), rf(H), rm(H), ar(H));
    std::fprintf(stderr,
      "  GATE  writeCommitInstructionBuffer=%llu\n", gate(H));
    std::fprintf(stderr,
      "  CL    opValid=%llu rtype=%llu dataMiss=%llu permMiss=%llu stale=%llu"
      " toReplay=%llu toInorderMiss=%llu flush=%llu wb=%llu walkerWb=%llu"
      " resGuardBlk=%llu cnt=%llu\n",
      opval(H), rtype(H), dmiss(H), pmiss(H), stale(H), torpl(H), tomi(H),
      flushs(H), wbv(H), wwbv(H), rgb(H), rgc(H));
    std::fprintf(stderr,
      "  CL.readBuffer   v=%llu brV=%llu addr=0x%09llx rob=%2llu wr=%llu insn=%08llx\n",
      rbv(H), rbbv(H), rba(H), rbr(H), rbw(H), rbi(H));
    std::fprintf(stderr,
      "  CL.replayBuffer v=%llu brV=%llu addr=0x%09llx rob=%2llu insn=%08llx\n",
      rpv(H), rpbv(H), rpa(H), rpr(H), rpi(H));
    std::fprintf(stderr,
      "  CL.missInorder  v=%llu brV=%llu addr=0x%09llx rob=%2llu insn=%08llx\n",
      miv(H), mibv(H), mia(H), mir(H), mii(H));
    std::fprintf(stderr,
      "  CL.missSpec     v=%llu brV=%llu addr=0x%09llx rob=%2llu insn=%08llx\n",
      msv(H), msbv(H), msa(H), msr(H), msi(H));
    std::fprintf(stderr,
      "  ARB   opState=%llu atomicBusy=%llu\n", ostate(H), abusy(H));
    std::fprintf(stderr,
      "  ARB.operation   v=%llu brV=%llu addr=0x%09llx rob=%2llu insn=%08llx\n",
      obv(H), obbv(H), oba(H), obr(H), obi(H));
    std::fprintf(stderr,
      "  ARB.inorder     v=%llu brV=%llu addr=0x%09llx rob=%2llu insn=%08llx\n",
      ibv(H), ibbv(H), iba(H), ibr(H), ibi(H));
    std::fprintf(stderr,
      "  ARB.speculative v=%llu brV=%llu addr=0x%09llx\n", sbv(H), sbbv(H), sba(H));
    std::fprintf(stderr,
      "  ACE   rdReq=%llu rdResp=%llu wr=%llu coh=%llu  mshrEmpty=%llu mshrFull=%llu\n",
      areq(H), ares(H), awr(H), acoh(H), msempty(H), msfull(H));
    std::fprintf(stderr,
      "  ACE.readBuffer  v=%llu brV=%llu addr=0x%09llx\n", arbv(H), arbbv(H), arba(H));
    std::fprintf(stderr,
      "  ACE.respBuffer  v=%llu brV=%llu addr=0x%09llx\n",
      arspv(H), arspbv(H), arspa(H));
    std::fprintf(stderr,
      "  ACE.writeBuffer v=%llu addr=0x%09llx   ACE.cohReq v=%llu addr=0x%09llx\n",
      awbv(H), awba(H), acrv(H), acra(H));
    dump_mshr(H);
    std::fprintf(stderr, "  SCHEDULER (dequeue=%llu idx=%llu)\n",
                 (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__scheduler__DOT__dequeue,
                 (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__scheduler__DOT__dequeuedIndex);
    dump_sched(H);
    dump_rq(H);
    std::fprintf(stderr,
      "  BRANCH  counter=%llu  branchPCs valid=%llu%llu%llu%llu"
      "  predictedPCs valid=%llu%llu%llu%llu\n",
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__branchCounter,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__branchPCs_0_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__branchPCs_1_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__branchPCs_2_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__branchPCs_3_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__predictedPCs_0_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__predictedPCs_1_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__predictedPCs_2_valid,
      (unsigned long long)tb_->system__DOT__chiron__DOT__core2__DOT__predictedPCs_3_valid);
    std::fprintf(stderr, "  ROB WINDOW (head=%llu tail=%llu)\n", frp(H), fwp(H));
    for (unsigned k = 0; k < 32; ++k) {
      const unsigned idx = (unsigned)((frp(H) + k) & 31);
      uint64_t pc = 0; uint32_t in = 0;
      rob_entry(H, idx, &pc, &in);
      std::fprintf(stderr, "    rob[%2u]%s pc=0x%09llx insn=%08x\n", idx,
                   k == 0 ? " <-HEAD" : "       ", (unsigned long long)pc, in);
    }
  };

  uint64_t cyc = 0, shut = 0, last_open = 0, last_pc = 0, pc_static = 0;
  // How much the miss path still MOVES while the gate is shut: a live-but-
  // looping replay looks completely different from a dead one.
  uint64_t n_replay = 0, n_missSet = 0, n_acereq = 0, n_opval = 0;
  // Silent-drop accounting. requestScheduler enqueues with `when(!fullReg)`
  // and requestIn is a fire-and-forget Reg two stages downstream of the
  // canAllocate backpressure the core scheduler samples -- so a request that
  // arrives at a full queue is dropped with no retry and its ROB entry waits
  // for a cache pass that will never happen.
  uint64_t n_storeReq = 0, n_storeDrop = 0, n_loadReq = 0, n_loadDrop = 0;
  uint64_t n_gateRise = 0, n_gateFire = 0, drop_cyc = 0, drop_addr = 0;
  unsigned prev_gate = 0;
  unsigned long long prev_areq = areq(H);

  while (cyc < END) {
    tb_->eval(); tb_->clock = 1; tb_->eval(); tb_->clock = 0; tb_->eval(); ++cyc;

    // The wedge is "this hart stopped retiring", not merely "no store passed
    // recently": early boot has long store-free stretches where the gate is
    // legitimately shut. Anchor on the committed PC going static instead.
    const uint64_t pc_now = bench.core_pc(H);
    if (pc_now != last_pc) { last_pc = pc_now; pc_static = 0; } else ++pc_static;

    {
      const unsigned g = (unsigned)gate(H);
      if (g && !prev_gate) ++n_gateRise;
      prev_gate = g;
      if (g && cfired(H)) ++n_gateFire;
      if (mrv(H)) {
        const unsigned op = (unsigned)mri(H) & 0x7f;
        const unsigned inFull = (unsigned)tb_->system__DOT__chiron__DOT__core2__DOT__memAccess__DOT__requestScheduler__DOT__inorderQueue__DOT__fullReg;
        const unsigned spFull = (unsigned)tb_->system__DOT__chiron__DOT__core2__DOT__memAccess__DOT__requestScheduler__DOT__speculativeQueue__DOT__fullReg;
        if (op == 0x23) {
          ++n_storeReq;
          if (inFull) { ++n_storeDrop; drop_cyc = cyc; drop_addr = mra(H); }
        } else if (op == 0x03) {
          ++n_loadReq;
          if (spFull && inFull) ++n_loadDrop;
        }
      }
    }

    if (mrv(H) || ostate(H) || obv(H) || ibv(H) || cfired(H)) {
      Ev &e = ring[rn++ % RING];
      e.cyc = cyc; e.mrv = (uint8_t)mrv(H); e.mra = mra(H); e.mri = (uint32_t)mri(H);
      e.ost = (uint8_t)ostate(H); e.obv = (uint8_t)obv(H); e.oba = oba(H);
      e.obi = (uint32_t)obi(H); e.ibv = (uint8_t)ibv(H); e.iba = iba(H);
      e.ibi = (uint32_t)ibi(H); e.gate = (uint8_t)gate(H); e.cf = (uint8_t)cfired(H);
      e.ca = (uint8_t)canall(H); e.hd = (uint8_t)hhd(H);
    }

    {
      const uint64_t key = (frp(H) << 24) | (fwp(H) << 16) | (rrp(H) << 8) | rwp(H);
      if (key != prev_key || fm(H) || rm(H) || cfired(H) || af(H) || bev(H)) {
        prev_key = key;
        Rv &r = rring[rrn++ % RING];
        r.cyc = cyc; r.frp = (uint8_t)frp(H); r.fwp = (uint8_t)fwp(H);
        r.rrp = (uint8_t)rrp(H); r.rwp = (uint8_t)rwp(H);
        r.fe = (uint8_t)fe(H); r.ff = (uint8_t)ff(H); r.fm = (uint8_t)fm(H);
        r.re = (uint8_t)re(H); r.rf = (uint8_t)rf(H); r.rm = (uint8_t)rm(H);
        r.cf = (uint8_t)cfired(H); r.af = (uint8_t)af(H); r.ar = (uint8_t)ar(H);
        r.bev = (uint8_t)bev(H); r.bep = (uint8_t)bep(H); r.ber = (uint8_t)ber(H);
      }
    }

    if (gate(H)) { shut = 0; last_open = cyc; }
    {
      ++shut;
      if (torpl(H)) ++n_replay;
      if (tomi(H))  ++n_missSet;
      if (opval(H)) ++n_opval;
      const unsigned long long a = areq(H);
      if (a != prev_areq) { ++n_acereq; prev_areq = a; }
      if (pc_static == STALL) {
        std::fprintf(stderr,
          "\n[storegate] hart%d stopped retiring for %llu cycles"
          " (gate last open at %llu)\n"
          "            while shut: opValid=%llu replayFire=%llu missSet=%llu"
          " aceReqStateChanges=%llu\n",
          H, (unsigned long long)shut, (unsigned long long)last_open,
          (unsigned long long)n_opval, (unsigned long long)n_replay,
          (unsigned long long)n_missSet, (unsigned long long)n_acereq);
        {
          uint64_t hpc = 0; uint32_t hin = 0;
          rob_entry(H, (unsigned)(frp(H) & 31), &hpc, &hin);
          std::fprintf(stderr,
            "  ROB HEAD slot %llu: pc=0x%llx insn=%08x  (opcode(6,4)=%u,"
            " store=%s)  commitReady=%llu commitFired=%llu canAllocate=%llu"
            " headHasData=%llu\n",
            frp(H), (unsigned long long)hpc, hin, (hin >> 4) & 7,
            (((hin >> 4) & 7) == 2) ? "YES" : "no",
            cready(H), cfired(H), canall(H), hhd(H));
        }
        std::fprintf(stderr,
          "[storegate] request accounting: storeReq=%llu storeDROPPED=%llu"
          " (last at cyc %llu addr 0x%llx) | loadReq=%llu loadDROPPED=%llu |"
          " gateRise=%llu gateFire=%llu\n",
          (unsigned long long)n_storeReq, (unsigned long long)n_storeDrop,
          (unsigned long long)drop_cyc, (unsigned long long)drop_addr,
          (unsigned long long)n_loadReq, (unsigned long long)n_loadDrop,
          (unsigned long long)n_gateRise, (unsigned long long)n_gateFire);
        snapshot(cyc, "STORE-GATE WEDGE");
        {
          const size_t have = rn < RING ? rn : RING;
          const size_t show = have < 400 ? have : 400;
          std::fprintf(stderr,
            "\n---- last %zu store-path events before the freeze ----\n"
            "cyc          mrV mrAddr        mrInsn   oSt obV obAddr        "
            "obInsn   ibV ibAddr        ibInsn   gate cf ca hd\n", show);
          for (size_t i = have - show; i < have; ++i) {
            const Ev &e = ring[(rn - have + i) % RING];
            std::fprintf(stderr,
              "%-12llu %1u  0x%09llx %08x  %1u  %1u  0x%09llx %08x  %1u "
              " 0x%09llx %08x  %1u   %1u %1u %1u\n",
              (unsigned long long)e.cyc, e.mrv, (unsigned long long)e.mra, e.mri,
              e.ost, e.obv, (unsigned long long)e.oba, e.obi,
              e.ibv, (unsigned long long)e.iba, e.ibi, e.gate, e.cf, e.ca, e.hd);
          }
        }
        std::fprintf(stderr, "\n---- %llu-cycle trace after the wedge ----\n"
          "cyc opV rt rbV rbAddr        dM pM rpl miV miAddr        "
          "oSt obV ibV areq ares awr coh mEmpty\n",
          (unsigned long long)TRACE);
        for (uint64_t t = 0; t < TRACE; ++t) {
          tb_->eval(); tb_->clock = 1; tb_->eval(); tb_->clock = 0; tb_->eval(); ++cyc;
          std::fprintf(stderr,
            "%3llu  %1llu %2llu  %1llu  0x%09llx %1llu %1llu %1llu  %1llu "
            "0x%09llx  %1llu   %1llu   %1llu   %1llu    %1llu    %1llu   %1llu   %1llu\n",
            (unsigned long long)t, opval(H), rtype(H), rbv(H), rba(H),
            dmiss(H), pmiss(H), torpl(H), miv(H), mia(H),
            ostate(H), obv(H), ibv(H), areq(H), ares(H), awr(H), acoh(H),
            msempty(H));
        }
        {
          const size_t have = rrn < RING ? rrn : RING;
          const size_t show = have < 120 ? have : 120;
          std::fprintf(stderr,
            "\n---- last %zu ROB pointer/rollback events ----\n"
            "cyc          fRd fWr fE fF fMod | rRd rWr rE rF rMod | cf af ar |"
            " brV brPass brRob\n", show);
          for (size_t i = have - show; i < have; ++i) {
            const Rv &r = rring[(rrn - have + i) % RING];
            std::fprintf(stderr,
              "%-12llu %3u %3u %2u %2u %4u | %3u %3u %2u %2u %4u | %2u %2u %2u |"
              " %3u %6u %5u\n",
              (unsigned long long)r.cyc, r.frp, r.fwp, r.fe, r.ff, r.fm,
              r.rrp, r.rwp, r.re, r.rf, r.rm, r.cf, r.af, r.ar,
              r.bev, r.bep, r.ber);
          }
        }
        snapshot(cyc, "after trace");
        std::fprintf(stderr, "\n[storegate] FAIL: hart%d wedged at cycle %llu\n",
                     H, (unsigned long long)cyc);
        std::fflush(stderr);
        return 1;
      }
    }
  }
  // Report the accounting on the clean path too: a dropped request is a
  // silent defect that only wedges the hart later (and only sometimes), so the
  // counter is the detector, not the wedge. storeDROPPED must be 0 -- see
  // Dcache/fifo.scala hasHeadroom.
  std::fprintf(stderr,
    "[storegate] hart%d ran %llu cycles without wedging\n"
    "            storeReq=%llu storeDROPPED=%llu | loadReq=%llu loadDROPPED=%llu"
    " | gateRise=%llu gateFire=%llu\n",
    H, (unsigned long long)END,
    (unsigned long long)n_storeReq, (unsigned long long)n_storeDrop,
    (unsigned long long)n_loadReq, (unsigned long long)n_loadDrop,
    (unsigned long long)n_gateRise, (unsigned long long)n_gateFire);
  if (n_storeDrop || n_loadDrop) {
    std::fprintf(stderr,
      "[storegate] FAIL: %llu request(s) dropped at a full D-cache queue"
      " (last at cyc %llu addr 0x%llx). The dropped access never reaches the"
      " cache, so its ROB entry waits forever -- this WILL wedge a hart.\n",
      (unsigned long long)(n_storeDrop + n_loadDrop),
      (unsigned long long)drop_cyc, (unsigned long long)drop_addr);
    return 1;
  }
  std::fprintf(stderr, "[storegate] PASS\n");
  return 0;
}
