// squashmiss_probe.cpp — why does the wrong-path load survive its squash?
//
// Established by measurement, in order:
//   prfwrite_probe : the writer that destroys a4 is PRF write port w3 (the
//                    D-cache load return), insn 0x18063783 = `ld a5,384(a2)`,
//                    writing p33 at cyc 9067568 with result 0x290.
//   the disassembly: 0x488 `ld a5,384(a2)` sits PAST the loop exit —
//                      470: addiw a4,a4,1
//                      474: bne   a4,a6,454   <- back-edge
//                      478: andi  a1,a1,2     <- fall-through, wrong path
//                      488: ld    a5,384(a2)
//                    so every iteration speculatively issues this load and then
//                    squashes it when the bne resolves taken. Its dest p33 is
//                    freed, reallocated to `addiw a4,a4,1` (which becomes a4's
//                    committed mapping), and the stale load then lands on it.
//   w3path_probe   : the response came through the CACHE path with
//                    cacheLookup.toResponse.request.branch.valid == 1, i.e. the
//                    squash never reached the request at all.
//
// So the remaining question is why the squash missed. The candidates:
//   (M) mask-bit recycling — regRecordUpdate (Dcache/utils.scala:128) only
//       squashes when (buffer.mask & branchOps.branchMask).orR. There are only
//       4 branch mask bits and this loop resolves a branch every iteration, so
//       a PASSING branch that reuses the bit clears it (line 135's XOR), and
//       the later failing bne then finds no overlap.
//   (Z) the load never carried a mask bit at all (mask == 0), so it looks
//       non-speculative to every squash check.
//   (S) the request sat in a structure branchOps is not applied to while the
//       branch resolved.
// These are distinguishable from the load's own branch mask over its lifetime
// against the stream of branch resolutions, which is what this probe prints.
//
// Build: make build/squashmiss_probe.out
// Run  : build/squashmiss_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig) tb_->system__DOT__chiron__DOT__core0__DOT__##sig
#define CL(sig) tb_->system__DOT__chiron__DOT__core0__DOT__memAccess__DOT__cacheLookup__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t STOP = env64("STOP", 9067600ULL);
  const uint64_t FROM = env64("FROM", 9067200ULL);
  const uint64_t INSN = env64("INSN", 0x18063783ULL);   // ld a5,384(a2)

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  unsigned pv = 9, pm = 9, pev = 9;      // previous readBuffer/replay/eval state
  unsigned prv = 9, prm = 9;

  while (cyc < STOP) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < FROM) continue;

    // Every branch resolution in the window, so a squash can be matched against
    // the load's mask bit by bit.
    const unsigned bev = (unsigned)C0(branchEvals_valid);
    if (bev) {
      printf("[cyc %llu] BRANCH %s  mask=%x rob=%u nextPC=%llx\n",
             (unsigned long long)cyc,
             (unsigned)C0(branchEvals_passed) ? "PASS" : "FAIL",
             (unsigned)C0(branchEvals_branchMask),
             (unsigned)C0(branchEvals_robAddr),
             (unsigned long long)C0(branchEvals_nextPC));
    }
    pev = bev;

    // The load's own state while it is held in cacheLookup.
    const unsigned rv = (unsigned)CL(readBuffer_valid);
    const uint64_t ri = (uint64_t)CL(readBuffer_core_instruction);
    if (rv && ri == INSN) {
      const unsigned bv = (unsigned)CL(readBuffer_branch_valid);
      const unsigned bm = (unsigned)CL(readBuffer_branch_mask);
      if (bv != pv || bm != pm) {
        printf("[cyc %llu]   readBuffer  ld a5,384(a2): branch.valid=%u "
               "mask=%x  prfDest=%u rob=%u addr=%llx\n",
               (unsigned long long)cyc, bv, bm,
               (unsigned)CL(readBuffer_core_prfDest),
               (unsigned)CL(readBuffer_core_robAddr),
               (unsigned long long)CL(readBuffer_address));
        pv = bv; pm = bm;
      }
    }

    const unsigned qv = (unsigned)CL(replayBuffer_valid);
    const uint64_t qi = (uint64_t)CL(replayBuffer_core_instruction);
    if (qv && qi == INSN) {
      const unsigned bv = (unsigned)CL(replayBuffer_branch_valid);
      const unsigned bm = (unsigned)CL(replayBuffer_branch_mask);
      if (bv != prv || bm != prm) {
        printf("[cyc %llu]   replayBuffer ld a5,384(a2): branch.valid=%u "
               "mask=%x  prfDest=%u rob=%u\n",
               (unsigned long long)cyc, bv, bm,
               (unsigned)CL(replayBuffer_core_prfDest),
               (unsigned)CL(replayBuffer_core_robAddr));
        prv = bv; prm = bm;
      }
    }
  }
  printf("\ndone cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
