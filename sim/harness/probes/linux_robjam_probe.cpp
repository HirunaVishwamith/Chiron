// linux_robjam_probe.cpp — is the 1,235M Linux freeze a jammed ROB head whose
// branch resolution was lost?
//
// 2026-08-12. The interrupt-injection FSM was a RED HERRING. Measured with
// linux_injfsm_probe: hart0's branchCounter took its last transition at cycle
// 1,230,905,042 (pc=81b03840, brCnt 0->1, inj=0 i.e. FSM idle) and never moved
// again. The IPI that the FSM later fails to inject does not arrive until
// ~1,235,243,841 — 4.3M cycles AFTER the hart already stopped committing. So the
// FSM stall is a symptom; the hart dies first, with a branch-class instruction
// decoded but never executed.
//
// Hypothesis under test (the reallocated-slot defect class, open as task #40):
// rob.scala writes completion straight to execPorts(i).robAddr / branch.robAddr
// with no check that the slot still belongs to the instruction that is
// completing. A resolution landing on a rolled-back-and-reallocated slot marks
// the WRONG entry done; the real branch at the head is never marked, and commit
// jams forever.
//
// What this prints at the jam:
//   * fifo (instruction) vs results (done-bit) head pointers and deq_valid —
//     head present but not done is the jam signature;
//   * the head instruction word — expected to be branch-class (opcode(6,4)=110);
//   * a ring buffer of every branch resolution (cyc, robAddr, passed) so a
//     resolution fired under the WRONG robAddr is visible as the head's address
//     never appearing, or appearing while the head stayed un-done.
//
// Build: make build/linux_robjam_probe.out
// Run:   CKPT_RESTORE=ckpt_prefix3_dualUnique_only/ckpt_001200000000.bin \
//          JAM_HART=0 ./build/linux_robjam_probe.out
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

int main() {
  const char *ckpt = getenv("CKPT_RESTORE");
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }
  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  // Default: stop once commit has been frozen this long. The freeze onset is
  // ~1,230,905,042, so a 2M-cycle stall confirms it without running to the IPI.
  const uint64_t END       = env64("END", 60000000ULL);
  const uint64_t STALL     = env64("STALL", 2000000ULL);
  const int HART           = getenv("JAM_HART") ? atoi(getenv("JAM_HART")) : 0;

  simulator bench;
  bench.init_no_image();
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0, msip_writes[4] = {}, last_uart_cyc = 0, uart_bytes = 0;
  {
    VerilatedRestore rs;
    rs.open(ckpt);
    if (!rs.isOpen()) { std::fprintf(stderr, "cannot open %s\n", ckpt); return 1; }
    rs >> cyc;
    rs >> msip_writes[0]; rs >> msip_writes[1];
    rs >> msip_writes[2]; rs >> msip_writes[3];
    rs >> last_uart_cyc;  rs >> uart_bytes;
    rs >> *tb_; rs.close();
  }
  const uint64_t start = cyc, deadline = cyc + END;
  std::fprintf(stderr, "restored at %llu, watching hart%d to %llu\n",
               (unsigned long long)start, HART, (unsigned long long)deadline);

#define PICK(sig) \
  [&](int n) -> unsigned long long { \
    switch (n) { case 0: return (unsigned long long)CORE_RAW(0, sig); \
                 case 1: return (unsigned long long)CORE_RAW(1, sig); \
                 case 2: return (unsigned long long)CORE_RAW(2, sig); \
                 default: return (unsigned long long)CORE_RAW(3, sig); } }
  auto fifoDeqV   = PICK(rob__DOT__fifo_io_deq_valid);
  auto resDeqV    = PICK(rob__DOT__results_io_deq_valid);
  // The M-unit. Linux's cpu_relax() is a DIVIDE on RISC-V, so every kernel spin
  // loop hammers this path -- and it has prior form here (divider back-to-back
  // wedge, divuw park, the extnMPartialServicing squash hole). A divide parked
  // with no divDone is a hart that stops committing while the LSU stays idle,
  // which is exactly the signature measured.
  auto divDone    = PICK(divDone);
  auto divDoneRob = PICK(divDoneRobAddr);
  auto mReqV      = PICK(extnMRequest_valid);
  auto mReqRob    = PICK(extnMRequest_robAddr);
  auto mReqInsn   = PICK(extnMRequest_instruction);
  auto mPartV     = PICK(extnMPartialServicing_valid);
  auto mPartRob   = PICK(extnMPartialServicing_robAddr);
  auto mPartInsn  = PICK(extnMPartialServicing_instruction);
  // THE decisive pair. mExtensionReady gates scheduler.release for EVERY M
  // instruction. It is set true only by a divide COMPLETING. If a mispredict
  // kills an in-flight divide (core.scala ageing block clears
  // division.request.valid without freeing the slot), the completion never runs
  // and the slot leaks forever:
  //   mExtensionReady=0 AND division.request.valid=0  =>  slot held by nobody,
  //   no divide can ever issue again, and the next divide's ROB entry jams the
  //   head exactly as measured.
  auto mExtRdy    = PICK(mExtensionReady);
  auto divReqV    = PICK(division_request_valid);
  auto divReqRob  = PICK(division_request_robAddr);
  auto divCounter = PICK(division_counter);
  auto divReqMask = PICK(division_request_branchMask);
  auto divBrMask  = PICK(divBranchMask);
  auto fenceSt    = PICK(memAccess__DOT__fenceState);
  auto subRdy     = PICK(memAccess__DOT__subModulesReady);
  auto cohAXI     = PICK(memAccess__DOT__aceUnit__DOT__coherentAXIState);
  auto rdReqSt    = PICK(memAccess__DOT__aceUnit__DOT__readACERequestState);
  auto rdRspSt    = PICK(memAccess__DOT__aceUnit__DOT__readACEResponseState);
  auto wrACESt    = PICK(memAccess__DOT__aceUnit__DOT__writeACEState);
  auto rdBufV     = PICK(memAccess__DOT__aceUnit__DOT__readBuffer_valid);
  auto rdBufA     = PICK(memAccess__DOT__aceUnit__DOT__readBuffer_address);
  auto commitRdy  = PICK(rob_commit_ready);
  auto allocRdy   = PICK(rob_allocate_ready);
  auto fifoRd     = PICK(rob__DOT__fifo__DOT__readPtr);
  auto fifoWr     = PICK(rob__DOT__fifo__DOT__writePtr);
  auto resRd      = PICK(rob__DOT__results__DOT__readPtr);
  auto resWr      = PICK(rob__DOT__results__DOT__writePtr);
  auto beValid    = PICK(branchEvals_valid);
  auto bePassed   = PICK(branchEvals_passed);
  auto beRobAddr  = PICK(branchEvals_robAddr);
  auto brCnt      = PICK(branchCounter);
  auto injSt      = PICK(interruptInjectStatus);
#undef PICK
  auto robpc = [&](int n) -> unsigned long long {
    switch (n) { case 0: return tb_->robOut0_pc; case 1: return tb_->robOut1_pc;
                 case 2: return tb_->robOut2_pc; default: return tb_->robOut3_pc; }
  };
  // Wide ROB entries are Verilator WData (uint32_t) ARRAYS -- fifo head is
  // 102 bits [4], results head is 130 bits [5]. They must be indexed; casting
  // the array to an integer yields the host POINTER, not the data (that mistake
  // produced a bogus "opcode=1c" on the first pass of this probe).
  auto fifoHeadArr = [&](int n) -> const uint32_t * {
    switch (n) { case 0: return CORE_RAW(0, rob__DOT__fifo__DOT__memReg_io_deq_bits_MPORT_data);
                 case 1: return CORE_RAW(1, rob__DOT__fifo__DOT__memReg_io_deq_bits_MPORT_data);
                 case 2: return CORE_RAW(2, rob__DOT__fifo__DOT__memReg_io_deq_bits_MPORT_data);
                 default: return CORE_RAW(3, rob__DOT__fifo__DOT__memReg_io_deq_bits_MPORT_data); }
  };
  auto resHeadArr = [&](int n) -> const uint32_t * {
    switch (n) { case 0: return CORE_RAW(0, rob__DOT__results__DOT__memReg_io_deq_bits_MPORT_data);
                 case 1: return CORE_RAW(1, rob__DOT__results__DOT__memReg_io_deq_bits_MPORT_data);
                 case 2: return CORE_RAW(2, rob__DOT__results__DOT__memReg_io_deq_bits_MPORT_data);
                 default: return CORE_RAW(3, rob__DOT__results__DOT__memReg_io_deq_bits_MPORT_data); }
  };

  // Every branch resolution, so we can ask afterwards whether the jammed head's
  // robAddr was ever resolved -- and if a resolution fired under a different
  // address while the head sat un-done.
  struct Res { uint64_t cyc; unsigned robAddr, passed; };
  const int RING = 96;
  std::vector<Res> ring(RING);
  int ringN = 0;

  uint64_t lastPc = robpc(HART), lastChange = cyc;
  uint64_t lastReport = cyc;
  bool jammed = false;

  while (cyc < deadline) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    if (beValid(HART)) {
      Res &r = ring[ringN % RING];
      r.cyc = cyc; r.robAddr = (unsigned)beRobAddr(HART);
      r.passed = (unsigned)bePassed(HART);
      ringN++;
    }

    const uint64_t pc = robpc(HART);
    if (pc != lastPc) { lastPc = pc; lastChange = cyc; }
    else if (cyc - lastChange >= STALL) { jammed = true; break; }

    if (cyc - lastReport >= 5000000) {
      lastReport = cyc;
      std::fprintf(stderr, "  [%llu] pc=%llx stalled_for=%llu\n",
                   (unsigned long long)cyc, (unsigned long long)pc,
                   (unsigned long long)(cyc - lastChange));
    }
  }

  if (!jammed) {
    std::printf("no commit stall >= %llu cycles by cyc=%llu\n",
                (unsigned long long)STALL, (unsigned long long)cyc);
    return 0;
  }

  std::printf("[JAM %llu] hart%d commit frozen at pc=%llx for %llu cycles\n\n",
              (unsigned long long)cyc, HART, (unsigned long long)lastPc,
              (unsigned long long)(cyc - lastChange));
  std::printf("ROB state:\n");
  std::printf("  fifo    (instructions): deq_valid=%llu readPtr=%llu writePtr=%llu\n",
              fifoDeqV(HART), fifoRd(HART), fifoWr(HART));
  std::printf("  results (done bits)   : deq_valid=%llu readPtr=%llu writePtr=%llu\n",
              resDeqV(HART), resRd(HART), resWr(HART));
  std::printf("  branchCounter=%llu  interruptInjectStatus=%llu"
              "  commit.ready=%llu allocate.ready=%llu\n",
              brCnt(HART), injSt(HART), commitRdy(HART), allocRdy(HART));
  std::printf("  (readPtr==writePtr with a valid entry => ROB FULL)\n\n");

  // THE discriminator: is the stuck head a branch awaiting resolution, or a
  // memory op the LSU never completed? rob.scala:102 shows the instruction word
  // sits at bits [12:6] for the opcode (commit.isStore compares bits(12,6)).
  // Full ROB contents. Entry layout (rob.scala): 102 bits =
  //   prfDest = bits(5,0), instruction = bits(37,6), pc = bits(101,38)
  // spread over 4 uint32_t words. Dumping all 16 beats guessing from one word.
  {
    std::printf("FULL ROB (head=readPtr=%llu, entries are pc / instruction):\n",
                fifoRd(HART));
    const uint32_t (*rob16)[4] =
        (HART == 1) ? CORE_RAW(1, rob__DOT__fifo__DOT__memReg)
      : (HART == 2) ? CORE_RAW(2, rob__DOT__fifo__DOT__memReg)
      : (HART == 3) ? CORE_RAW(3, rob__DOT__fifo__DOT__memReg)
                    : CORE_RAW(0, rob__DOT__fifo__DOT__memReg);
    for (int i = 0; i < 16; ++i) {
      const uint32_t w0 = rob16[i][0], w1 = rob16[i][1],
                     w2 = rob16[i][2], w3 = rob16[i][3];
      const uint32_t insn = (w0 >> 6) | (uint32_t)((w1 & 0x3F) << 26);
      const uint64_t pc = ((uint64_t)(w1 >> 6)) |
                          ((uint64_t)w2 << 26) |
                          ((uint64_t)(w3 & 0x3F) << 58);
      const unsigned op = insn & 0x7f;
      const char *k = ((op >> 4) == 0x6) ? "BR/JAL/JALR"
                    : (op == 0x03) ? "LOAD" : (op == 0x23) ? "STORE"
                    : (op == 0x2f) ? "AMO"  : (op == 0x73) ? "SYSTEM"
                    : (op == 0x0f) ? "FENCE": "ALU/other";
      std::printf("  [%2d]%s pc=%016llx insn=%08x op=%02x %s\n", i,
                  (i == (int)fifoRd(HART)) ? " <=HEAD" : "       ",
                  (unsigned long long)pc, insn, op, k);
    }
    std::printf("\n");
  }

  // Scheduler occupancy. If the ROB is full while the scheduler is EMPTY, the
  // two are desynchronised: the ROB is holding entries that no longer exist
  // anywhere that could issue them, so nothing will ever complete.
  {
    std::printf("SCHEDULER queue (8 slots):\n");
    int occupied = 0;
#define SQ(n, f) ((HART == 1) ? (unsigned long long)CORE_RAW(1, scheduler__DOT__queue_##n##_##f) \
                : (HART == 2) ? (unsigned long long)CORE_RAW(2, scheduler__DOT__queue_##n##_##f) \
                : (HART == 3) ? (unsigned long long)CORE_RAW(3, scheduler__DOT__queue_##n##_##f) \
                              : (unsigned long long)CORE_RAW(0, scheduler__DOT__queue_##n##_##f))
#define SHOW(n) do { \
      const unsigned long long v = SQ(n, valid); \
      if (v) occupied++; \
      std::printf("  [%d] valid=%llu robAddr=%-3llu insn=%08llx\n", n, v, \
                  SQ(n, robAddr), SQ(n, instruction)); } while (0)
    SHOW(0); SHOW(1); SHOW(2); SHOW(3); SHOW(4); SHOW(5); SHOW(6); SHOW(7);
#undef SHOW
#undef SQ
    std::printf("  occupied=%d  releasedBuffer.valid=%llu\n", occupied,
                (unsigned long long)((HART == 1) ? CORE_RAW(1, scheduler__DOT__releasedBuffer_valid)
                                   : (HART == 2) ? CORE_RAW(2, scheduler__DOT__releasedBuffer_valid)
                                   : (HART == 3) ? CORE_RAW(3, scheduler__DOT__releasedBuffer_valid)
                                                 : CORE_RAW(0, scheduler__DOT__releasedBuffer_valid)));
    if (occupied == 0)
      std::printf("  *** ROB FULL but SCHEDULER EMPTY => ROB/scheduler desync.\n"
                  "      Nothing can ever issue; the head can never complete.\n");
    else
      std::printf("  scheduler still holds work -- check whether the head's\n"
                  "  robAddr appears above; if not, its entry was lost.\n");
    std::printf("\n");
  }

  const uint32_t *fhp = fifoHeadArr(HART);
  const uint32_t *rhp = resHeadArr(HART);
  const unsigned long long fh = (unsigned long long)fhp[0] |
                                ((unsigned long long)fhp[1] << 32);
  const unsigned long long rh = (unsigned long long)rhp[0] |
                                ((unsigned long long)rhp[1] << 32);
  const unsigned opcode = (unsigned)((fh >> 6) & 0x7f);
  const char *kind =
      ((opcode >> 4) == 0x6)      ? "BRANCH/JAL/JALR (awaiting resolution)"
    : (opcode == 0x03)            ? "LOAD  (awaiting LSU)"
    : (opcode == 0x23)            ? "STORE (awaiting LSU)"
    : (opcode == 0x2f)            ? "AMO/LR/SC (awaiting LSU)"
    : (opcode == 0x73)            ? "SYSTEM"
    : (opcode == 0x0f)            ? "FENCE"
                                  : "other/ALU";
  std::printf("HEAD instruction: raw=%016llx opcode=%02x  -> %s\n", fh, opcode, kind);
  std::printf("HEAD result     : raw=%016llx  doneBit=%llu exception=%llu\n",
              rh, rh & 1ULL, (rh >> 1) & 1ULL);
  std::printf("  doneBit=0 => this instruction never completed; that is the jam.\n\n");

  std::printf("hart%d LSU / coherence state (is a memory op stuck?):\n", HART);
  std::printf("  fenceState=%llu subModulesReady=%llu\n", fenceSt(HART), subRdy(HART));
  std::printf("  ACE: coherentAXI=%llu readReq=%llu readRsp=%llu writeACE=%llu\n",
              cohAXI(HART), rdReqSt(HART), rdRspSt(HART), wrACESt(HART));
  std::printf("  readBuffer: valid=%llu address=%llx\n\n",
              rdBufV(HART), rdBufA(HART));

  std::printf("hart%d M-unit (cpu_relax() is a DIVIDE on RISC-V):\n", HART);
  std::printf("  divDone=%llu divDoneRobAddr=%llu\n", divDone(HART), divDoneRob(HART));
  std::printf("  extnMRequest:           valid=%llu robAddr=%llu insn=%08llx\n",
              mReqV(HART), mReqRob(HART), mReqInsn(HART));
  std::printf("  extnMPartialServicing:  valid=%llu robAddr=%llu insn=%08llx\n",
              mPartV(HART), mPartRob(HART), mPartInsn(HART));
  std::printf("  mExtensionReady=%llu   <== gates release of EVERY M instruction\n",
              mExtRdy(HART));
  std::printf("  division.request: valid=%llu robAddr=%llu counter=%llu mask=%llx\n",
              divReqV(HART), divReqRob(HART), divCounter(HART), divReqMask(HART));
  std::printf("  divBranchMask=%llx\n", divBrMask(HART));
  if (!mExtRdy(HART) && !divReqV(HART)) {
    std::printf("  *** LEAKED M-SLOT: mExtensionReady=0 with NO divide in the\n");
    std::printf("      divider. Nothing can ever set it true again (only a\n");
    std::printf("      completion does), so no M instruction can ever issue.\n");
    std::printf("      A mispredict killed an in-flight divide without freeing\n");
    std::printf("      the slot -- core.scala divider ageing block.\n");
  } else if (divReqV(HART) && divCounter(HART) == 0) {
    std::printf("  *** divide present with counter=0 but no completion fired.\n");
  } else if (divReqV(HART)) {
    std::printf("  *** divide still iterating (counter=%llu) -- not a leak.\n",
                divCounter(HART));
  } else {
    std::printf("  M-unit idle and slot free; the jam is NOT the divider.\n");
  }
  std::printf("\n");

  const unsigned head = (unsigned)fifoRd(HART);
  const int n = ringN < RING ? ringN : RING;
  const int first = ringN < RING ? 0 : ringN % RING;
  std::printf("last %d branch resolutions on hart%d (oldest first);"
              " head robAddr=%u\n", n, HART, head);
  std::printf("%-13s %-9s %-7s %s\n", "cyc", "robAddr", "passed", "== head?");
  int hits = 0;
  for (int i = 0; i < n; ++i) {
    const Res &r = ring[(first + i) % RING];
    const bool isHead = (r.robAddr == head);
    if (isHead) hits++;
    std::printf("%-13llu %-9u %-7u %s\n", (unsigned long long)r.cyc,
                r.robAddr, r.passed, isHead ? "  <== HEAD" : "");
  }
  std::printf("\nresolutions targeting the jammed head: %d\n", hits);
  std::printf("  0 hits  => the head's resolution never fired (lost, or emitted\n");
  std::printf("             under a different robAddr -- reallocated-slot defect)\n");
  std::printf("  >0 hits => it DID fire; the done-bit write is being dropped or\n");
  std::printf("             overwritten instead, so look at results.writeports\n");
  return 0;
}
