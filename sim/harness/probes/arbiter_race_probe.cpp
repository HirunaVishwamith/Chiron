// arbiter_race_probe.cpp — is the D-cache arbiter's operationBuffer handshake
// racing its own operation FSM?
//
// Static reading of Dcache/arbiter.scala says yes, in two ways:
//
//   operationBufferReadyWire = !operationBuffer.valid ||
//                              (operationBuffer.valid && !branch.valid)
//   request.inorderReady    := inorderBufferReadyWire && operationBufferReadyWire
//
// i.e. a SQUASHED operationBuffer entry is advertised upstream as "free", and
// requestScheduler will hand a new inorder request over. But ownership of
// operationBuffer belongs to the operation FSM, which keys off
// operationWires.valid == operationBuffer.valid and never looks at branch.valid.
// So on such a cycle BOTH fire:
//
//   line 81   operationBuffer := request.request     (enqueue, elaborated 1st)
//   line 116  operationBuffer.valid := false.B       (FSM drains, elaborated 2nd)
//
// and Chisel last-connect makes the FSM win -> the freshly accepted request is
// destroyed. It was never dispatched, so its response never returns and the ROB
// entry never completes: the hart stops committing. That is the mt-ipimux
// signature (c1=0 commits, MSIP pending unserviced).
//
// The atomic arm (line 122) is worse: it does NOT clear operationBuffer, and
// moves the FSM to waitState, which spins on
//   responseOut.instruction === operationBuffer.core.instruction
// against whatever instruction the enqueue just overwrote it with -> the FSM
// can never leave waitState -> permanent wedge.
//
// This probe counts both coincidences per core and prints the first few with
// full context, so the mechanism is measured rather than argued.
//
// Build: make build/arbiter_race_probe.out
// Run  : build/arbiter_race_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define AR(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__arbiter__DOT__##s
#define MA(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__##s

// operationState encoding, from `Enum(5)` in arbiter.scala
static const char *STNAME[6] = {"idle", "commitReady", "commitFired",
                                "wait", "writeInsnFired", "?"};

struct Stat {
  uint64_t overwrite = 0;   // enqueue while operationBuffer still occupied
  uint64_t busystate = 0;   // enqueue while FSM is not in idleState
  uint64_t printed = 0;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END     = env64("END", 30000000ULL);
  const uint64_t MAXPRINT = env64("MAXPRINT", 6ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  Stat st[4];
  uint64_t cyc = 0;

  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

#define CHECK(n)                                                              \
    do {                                                                      \
      const unsigned enq = (unsigned)MA(n, requestScheduler_requestOut_valid) \
          && (unsigned)MA(n, requestScheduler_requestOut_branch_valid)        \
          && !(unsigned)MA(n, requestScheduler_controlSignal_isSpeculative);  \
      if (enq) {                                                              \
        const unsigned obv = (unsigned)AR(n, operationBuffer_valid);          \
        const unsigned obb = (unsigned)AR(n, operationBuffer_branch_valid);   \
        const unsigned stt = (unsigned)AR(n, operationState);                 \
        if (obv) st[n].overwrite++;                                           \
        if (stt != 0) st[n].busystate++;                                      \
        if ((obv || stt != 0) && st[n].printed < MAXPRINT) {                  \
          st[n].printed++;                                                    \
          printf("[cyc %llu] core%d ENQ over live operationBuffer: "          \
                 "state=%s occupant{valid=%u branch.valid=%u mask=%02x "      \
                 "insn=%08x rob=%u} incoming{insn=%08x rob=%u mask=%02x}\n",  \
                 (unsigned long long)cyc, n, STNAME[stt < 5 ? stt : 5],       \
                 obv, obb, (unsigned)AR(n, operationBuffer_branch_mask),      \
                 (unsigned)AR(n, operationBuffer_core_instruction),           \
                 (unsigned)AR(n, operationBuffer_core_robAddr),               \
                 (unsigned)MA(n, requestScheduler_requestOut_core_instruction),\
                 (unsigned)MA(n, requestScheduler_requestOut_core_robAddr),   \
                 (unsigned)MA(n, requestScheduler_requestOut_branch_mask));   \
        }                                                                     \
      }                                                                       \
    } while (0)
    CHECK(0); CHECK(1); CHECK(2); CHECK(3);
#undef CHECK
  }

  printf("\n=== arbiter operationBuffer enqueue/FSM coincidences (%llu cycles) ===\n",
         (unsigned long long)cyc);
  for (int n = 0; n < 4; n++)
    printf("  core%d: enq-over-occupied=%llu  enq-while-FSM-busy=%llu\n", n,
           (unsigned long long)st[n].overwrite,
           (unsigned long long)st[n].busystate);

  printf("\n=== final arbiter state ===\n");
#define DUMP(n)                                                               \
  printf("  core%d state=%-14s opBuf{v=%u bv=%u insn=%08x} "                  \
         "inBuf{v=%u bv=%u insn=%08x} specBuf{v=%u bv=%u} atomicBusy=%u\n",   \
         n, STNAME[(unsigned)AR(n, operationState) < 5                        \
                   ? (unsigned)AR(n, operationState) : 5],                    \
         (unsigned)AR(n, operationBuffer_valid),                              \
         (unsigned)AR(n, operationBuffer_branch_valid),                       \
         (unsigned)AR(n, operationBuffer_core_instruction),                   \
         (unsigned)AR(n, inorderBuffer_valid),                                \
         (unsigned)AR(n, inorderBuffer_branch_valid),                         \
         (unsigned)AR(n, inorderBuffer_core_instruction),                     \
         (unsigned)AR(n, speculativeBuffer_valid),                            \
         (unsigned)AR(n, speculativeBuffer_branch_valid),                     \
         (unsigned)AR(n, atomicBusyState))
  DUMP(0); DUMP(1); DUMP(2); DUMP(3);
#undef DUMP
  return 0;
}
