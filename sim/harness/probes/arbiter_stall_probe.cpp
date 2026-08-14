// arbiter_stall_probe.cpp — does the D-cache arbiter's operation FSM have a
// state it can never leave?
//
// arbiter.scala's atomic path parks the AMO/LR/SC in operationBuffer and waits
// for its own response to come back:
//
//   is(waitState){
//     operationState := Mux(responseOut.valid &&
//                           responseOut.instruction === operationBuffer.core.instruction,
//                           commitReadyState, waitState)
//   }
//
// There is no squash escape. If the atomic is branch-killed while parked here,
// regRecordUpdate (arbiter.scala:158) clears operationBuffer.branch.valid, and
// cacheLookup will never emit a response for it (cacheModule.scala:160 gates
// toResponse on branch.valid) -> the compare above can never be satisfied ->
// the FSM stays in waitState forever.
//
// It gets worse from there: operationBufferReadyWire treats a squashed entry as
// FREE, so requestScheduler keeps handing new inorder requests over, they land
// in operationBuffer (arbiter.scala:81), and waitState now compares against
// whatever instruction just overwrote it. Meanwhile nothing moves
// operationBuffer -> inorderBuffer outside idleState, so that new request is
// never dispatched either. The hart stops committing entirely — which is the
// mt-ipimux signature (c1=0 commits, MSIP pending unserviced).
//
// This probe measures dwell time in every non-idle operation-FSM state per
// core, and separately counts cycles where operationBuffer sits SQUASHED while
// the FSM is outside idleState (the precursor). A max dwell in the millions is
// the wedge; a nonzero squashed-while-busy count is the window being entered.
//
// Build: make build/arbiter_stall_probe.out
// Run  : build/arbiter_stall_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define AR(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__arbiter__DOT__##s
#define CORE(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__##s

static const char *STNAME[6] = {"idle", "commitReady", "commitFired",
                                "wait", "writeInsnFired", "?"};

struct Stat {
  unsigned  cur_state   = 0;
  uint64_t  state_since = 0;
  uint64_t  max_dwell[5] = {0, 0, 0, 0, 0};
  uint64_t  max_at[5]    = {0, 0, 0, 0, 0};
  uint64_t  squashed_while_busy = 0;   // cycles: opBuf valid, branch dead, FSM busy
  uint64_t  first_squash_busy   = 0;
  uint64_t  printed = 0;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END      = env64("END", 30000000ULL);
  // Report (once per core) as soon as a non-idle dwell exceeds this.
  const uint64_t ALARM    = env64("ALARM", 200000ULL);
  const uint64_t MAXPRINT = env64("MAXPRINT", 4ULL);

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
      const unsigned s   = (unsigned)AR(n, operationState);                   \
      const unsigned obv = (unsigned)AR(n, operationBuffer_valid);            \
      const unsigned obb = (unsigned)AR(n, operationBuffer_branch_valid);     \
      if (s != st[n].cur_state) {                                             \
        const uint64_t dwell = cyc - st[n].state_since;                       \
        const unsigned prev  = st[n].cur_state;                               \
        if (prev < 5 && dwell > st[n].max_dwell[prev]) {                      \
          st[n].max_dwell[prev] = dwell;                                      \
          st[n].max_at[prev]    = st[n].state_since;                          \
        }                                                                     \
        st[n].cur_state = s; st[n].state_since = cyc;                         \
      } else if (s != 0 && cyc - st[n].state_since == ALARM &&                \
                 st[n].printed < MAXPRINT) {                                  \
        st[n].printed++;                                                      \
        printf("[cyc %llu] core%d STUCK in operationState=%s for %llu cycles: "\
               "opBuf{v=%u branch.valid=%u mask=%02x insn=%08x rob=%u} "      \
               "inBuf{v=%u bv=%u} atomicBusy=%u respOut{v=%u insn=%08x}\n",   \
               (unsigned long long)cyc, n, STNAME[s < 5 ? s : 5],             \
               (unsigned long long)ALARM, obv, obb,                           \
               (unsigned)AR(n, operationBuffer_branch_mask),                  \
               (unsigned)AR(n, operationBuffer_core_instruction),             \
               (unsigned)AR(n, operationBuffer_core_robAddr),                 \
               (unsigned)AR(n, inorderBuffer_valid),                          \
               (unsigned)AR(n, inorderBuffer_branch_valid),                   \
               (unsigned)AR(n, atomicBusyState),                              \
               (unsigned)CORE(n, memAccess_responseOut_valid),                \
               (unsigned)CORE(n, memAccess_responseOut_instruction));         \
      }                                                                       \
      if (s != 0 && obv && !obb) {                                            \
        if (!st[n].squashed_while_busy) st[n].first_squash_busy = cyc;        \
        st[n].squashed_while_busy++;                                          \
      }                                                                       \
    } while (0)
    CHECK(0); CHECK(1); CHECK(2); CHECK(3);
#undef CHECK
  }

  printf("\n=== arbiter operation-FSM dwell over %llu cycles ===\n",
         (unsigned long long)cyc);
  for (int n = 0; n < 4; n++) {
    // fold in the state we ended in
    const unsigned s = st[n].cur_state;
    const uint64_t d = cyc - st[n].state_since;
    if (s < 5 && d > st[n].max_dwell[s]) { st[n].max_dwell[s] = d; st[n].max_at[s] = st[n].state_since; }
    printf("  core%d max dwell:", n);
    for (int k = 1; k < 5; k++)
      printf("  %s=%llu@%llu", STNAME[k], (unsigned long long)st[n].max_dwell[k],
             (unsigned long long)st[n].max_at[k]);
    printf("\n         squashed-opBuf-while-FSM-busy=%llu cycles (first @%llu), "
           "ended in %s\n",
           (unsigned long long)st[n].squashed_while_busy,
           (unsigned long long)st[n].first_squash_busy,
           STNAME[st[n].cur_state < 5 ? st[n].cur_state : 5]);
  }
  return 0;
}
