// brswallow_probe.cpp — count branch resolutions swallowed by core.scala:625-630.
//
// branchEvals is a REGISTER (core.scala:584) and branchOps is a plain Wire fed
// from its CURRENT value (core.scala:591-593). So this block:
//
//   branchEvals.valid := Mux(coherentLoadInvalid, true.B, branchInstruction.valid)
//   when(branchOps.valid) { when(!branchOps.passed) { branchEvals.valid := false.B } }
//
// means "on the cycle AFTER a failed branch resolution, force branchEvals.valid
// to 0" — unconditionally. The generated Verilog is explicit:
//
//   end else if (branchEvals_valid) begin        // core.scala 625
//     if (_T_2) branchEvals_valid <= 1'h0;       // core.scala 628
//     else      branchEvals_valid <= branchInstruction_valid;   // core.scala 621
//
// The intent is to not emit a resolution for a branch on the squashed path, but
// that case is ALREADY handled correctly one block above (core.scala:608-614),
// which kills branchInstruction.valid only when the branch's own mask overlaps
// the failing mask. The version here has no such qualification, so it also
// swallows the resolution of a branch that is NOT on the squashed path — an
// older or independent branch that merely happened to issue on that cycle.
//
// A swallowed resolution is unrecoverable:
//   - branchCounter (core.scala:1102-1106) never gets its decrement,
//   - rob.branch.valid (core.scala:799) never marks the ROB entry done,
//   - the branch's mask bit is never cleared for its dependents.
// The hart then stops committing, and if an IPI arrives the injection FSM parks
// in waitToInjectInterr forever waiting for branchCounter to reach 0 while
// core.scala:1197 holds fetch/decode down — exactly the mt-ipimux wedge.
//
// A SWALLOW event here is: previous cycle had (branchEvals.valid &&
// !branchEvals.passed), and this cycle branchInstruction.valid is still 1.
//
// That second term is itself the proof the branch was live: core.scala:612-614
// forces branchInstruction.valid to false whenever the failing mask overlaps
// the issuing instruction's own mask. So a branchInstruction.valid that is
// STILL 1 while a failure is being broadcast is by construction a branch the
// mispredict does not cover — a legitimate resolution about to be thrown away
// by the unqualified kill at core.scala:628.
//
// Build: make build/brswallow_probe.out
// Run  : build/brswallow_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__##s

struct Stat {
  uint64_t swallow_any = 0;   // resolution lost, branch was squashed anyway
  uint64_t swallow_live = 0;  // resolution lost for a branch NOT on the squashed path
  uint64_t first_live = 0;
  uint64_t printed = 0;
  uint64_t delivered = 0;   // resolution survived to the next cycle (fix live)
  uint64_t killed = 0;      // resolution discarded (unfixed behaviour)
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END      = env64("END", 5000000ULL);
  const uint64_t MAXPRINT = env64("MAXPRINT", 6ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  Stat st[4];
  // previous-cycle branchEvals state per core
  unsigned pv[4] = {0,0,0,0}, pp[4] = {0,0,0,0}, pm[4] = {0,0,0,0};
  unsigned armed[4] = {0,0,0,0};   // last cycle was a swallow-window cycle
  uint64_t cyc = 0;

  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

#define CHECK(n)                                                              \
    do {                                                                      \
      const unsigned biv = (unsigned)C(n, branchInstruction_valid);           \
      const unsigned bev = (unsigned)C(n, branchEvals_valid);                 \
      const unsigned bps = (unsigned)C(n, branchEvals_passed);                \
      /* SAME cycle: the register update at core.scala:625-628 reads          \
         branchEvals_valid/passed and branchInstruction_valid all at cycle T  \
         and drives branchEvals_valid(T+1). So a FAILING resolution in flight \
         at T discards the branch sitting in the unit at T.                   \
         branchInstruction.valid still being 1 means core.scala:613 did NOT   \
         squash it, i.e. the mispredict does not cover this branch: a live    \
         resolution is lost. */                                              \
      if (armed[n]) {                                                         \
        armed[n] = 0;                                                         \
        if (bev) st[n].delivered++; else st[n].killed++;                       \
      }                                                                       \
      if (bev && !bps && biv) {                                               \
        armed[n] = 1;                                                         \
        st[n].swallow_any++;                                                  \
        if (!st[n].swallow_live) st[n].first_live = cyc;                      \
        st[n].swallow_live++;                                                 \
        if (st[n].printed < MAXPRINT) {                                       \
          st[n].printed++;                                                    \
          printf("[cyc %llu] core%d SWALLOWED live resolution: "              \
                 "branchInstruction{valid=1 insn=%08x rob=%u ownMask=%02x} "  \
                 "killed by previous-cycle FAIL(mask=%02x) that does not "    \
                 "cover it\n",                                               \
                 (unsigned long long)cyc, n,                                  \
                 (unsigned)C(n, branchInstruction_instruction),               \
                 (unsigned)C(n, branchInstruction_robAddr),                   \
                 (unsigned)C(n, branchPCs_0_branchMask),                       \
                 (unsigned)C(n, branchEvals_branchMask));                     \
        }                                                                     \
      }                                                                       \
      pv[n] = (unsigned)C(n, branchEvals_valid);                              \
      pp[n] = (unsigned)C(n, branchEvals_passed);                             \
      pm[n] = (unsigned)C(n, branchEvals_branchMask);                         \
    } while (0)
    CHECK(0); CHECK(1); CHECK(2); CHECK(3);
#undef CHECK
  }

  printf("\n=== swallowed branch resolutions over %llu cycles ===\n",
         (unsigned long long)cyc);
  for (int n = 0; n < 4; n++)
    printf("  core%d: kill-window-hits=%llu (first @%llu) -> next cycle: "
           "resolution DELIVERED=%llu  DISCARDED=%llu\n", n,
           (unsigned long long)st[n].swallow_any,
           (unsigned long long)st[n].first_live,
           (unsigned long long)st[n].delivered,
           (unsigned long long)st[n].killed);
  return 0;
}
