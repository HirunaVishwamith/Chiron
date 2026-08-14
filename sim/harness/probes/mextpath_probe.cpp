// mextpath_probe.cpp — WHICH M-extension unit drove the bogus completion?
//
// robready_probe established that mt-ipimux's hart1 wedge is caused by a single
// event: at cyc 26814085 the ROB ready bit of slot 12 (holding a BRANCH,
// fe071ae3 `bne a4,zero`) goes 0->1 with no branchEvals naming it, attributed to
// the M-extension port (extnMResponse.valid=1, robAddr=12). The branch then
// retires on that false bit at 26814115; its REAL resolution lands at 26814118,
// fails, flushes the scheduler, and the next ROB head never completes.
//
// extnMResponse has TWO drivers and they are very different code paths:
//   (a) the MULTIPLY pipeline, core.scala:392
//         extnMResponse.valid := extnMServicing.valid
//       fed prf.toExec -> extnMRequest -> extnMPartialServicing -> extnMServicing,
//   (b) the DIVIDER completion, core.scala:503
//         when(division.request.valid && !division.counter.orR) { ... }
//       which bypasses the multiply stages entirely (core.scala:237 forces
//       extnMPartialServicing.valid low for div/rem, instruction bit 14).
//
// Fixing the multiply pipeline's squash mis-pairing left the event BIT-IDENTICAL,
// which already points at (b) — but "unchanged" is weak evidence, so measure it
// directly instead of inferring. extnMResponse is a register, so a response
// observed at cycle T was driven at T-1: this dumps both candidate drivers every
// cycle and, at the moment of the bogus response, reports which one was live.
//
// It also reconstructs the divide's provenance, which is what a fix needs:
//   * ARM   — when division.request was loaded, with which robAddr and mask
//             (core.scala:459-480),
//   * AGE   — every branchOps pulse that rewrites division.request.branchMask
//             (core.scala:519, 522) or kills it (520, 523),
// so we can see whether the in-flight divide's mask tracked the branch that
// should have squashed it. Suspicion under test: on the ARM cycle, line 522 keys
// off the OLD division.request.branchMask register and can clobber the mask
// line 519 just installed for the newly armed request.
//
// PRE-EDGE sampling (after eval(), before toggling clock) — see robmodify_probe.
//
// Build: make build/mextpath_probe.out
// Run  : FROM=26813900 TO=26814200 TARGET=12 build/mextpath_probe.out \
//            bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C(s) tb_->system__DOT__chiron__DOT__core1__DOT__##s

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t FROM   = env64("FROM", 26813900ULL);
  const uint64_t TO     = env64("TO",   26814200ULL);
  const unsigned TARGET = (unsigned)env64("TARGET", 12ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  // previous-cycle driver state: a response visible at T was driven at T-1.
  unsigned pServV = 0, pServRob = 0, pServMask = 0;
  unsigned pDivV = 0, pDivRob = 0, pDivMask = 0, pDivCnt = 0;
  unsigned pRespV = 0, pRespRob = 0;
  unsigned pReqV = 0, pReqRob = 0, pReqMask = 0, pReqInsn = 0;
  uint64_t nMulDrove = 0, nDivDrove = 0, nNeither = 0;

  while (cyc < TO) {
    tb_->eval();

    const unsigned respV   = (unsigned)C(extnMResponse_valid);
    const unsigned respRob = (unsigned)C(extnMResponse_robAddr);
    const unsigned servV   = (unsigned)C(extnMServicing_valid);
    const unsigned servRob = (unsigned)C(extnMServicing_robAddr);
    const unsigned servMask= (unsigned)C(extnMServicing_branchMask);
    const unsigned divV    = (unsigned)C(division_request_valid);
    const unsigned divRob  = (unsigned)C(division_request_robAddr);
    const unsigned divMask = (unsigned)C(division_request_branchMask);
    const unsigned divCnt  = (unsigned)C(division_counter);
    const unsigned reqV    = (unsigned)C(extnMRequest_valid);
    const unsigned reqRob  = (unsigned)C(extnMRequest_robAddr);
    const unsigned reqMask = (unsigned)C(extnMRequest_branchMask);
    const unsigned reqInsn = (unsigned)C(extnMRequest_instruction);
    const unsigned bev     = (unsigned)C(branchEvals_valid);
    const unsigned bpas    = (unsigned)C(branchEvals_passed);
    const unsigned brob    = (unsigned)C(branchEvals_robAddr);
    const unsigned bmask   = (unsigned)C(branchEvals_branchMask);

    // A response rising this cycle was driven by whichever unit was live last
    // cycle. Attribute it, and shout when it names the wedging slot.
    if (respV && !pRespV) {
      const bool mulDrove = pServV && (pServRob == respRob);
      const bool divDrove = pDivV && !pDivCnt && (pDivRob == respRob);
      if (mulDrove && !divDrove) nMulDrove++;
      else if (divDrove && !mulDrove) nDivDrove++;
      else if (!mulDrove && !divDrove) nNeither++;
      if (respRob == TARGET && cyc >= FROM && cyc <= TO)
        printf("[cyc %llu] ** M-EXT RESPONSE rob=%u  driver: %s "
               "[mul: serv{v=%u rob=%u mask=0x%x}] "
               "[div: req{v=%u rob=%u mask=0x%x cnt=%u}]\n",
               (unsigned long long)cyc, respRob,
               (mulDrove && divDrove) ? "AMBIGUOUS (both live)"
                 : mulDrove ? "MULTIPLY pipeline"
                 : divDrove ? "DIVIDER" : "NEITHER (!)",
               pServV, pServRob, pServMask, pDivV, pDivRob, pDivMask, pDivCnt);
    }

    if (cyc >= FROM && cyc <= TO) {
      // ARM: division.request loaded from extnMRequest (core.scala:459-480).
      // Visible as the request register changing to a live divide.
      if (divV && (!pDivV || divRob != pDivRob))
        printf("[cyc %llu] DIV-ARM    rob=%u mask=0x%x cnt=%u  (from req rob=%u "
               "mask=0x%x insn=%08x)\n",
               (unsigned long long)cyc, divRob, divMask, divCnt,
               pReqRob, pReqMask, pReqInsn);
      // AGE/KILL: any branch resolution that should touch the in-flight divide.
      if (bev && pDivV)
        printf("[cyc %llu] BRANCH %s rob=%2u mask=0x%x | div{v=%u rob=%u "
               "mask=0x%x cnt=%u} overlap=%d\n",
               (unsigned long long)cyc, bpas ? "PASS" : "FAIL", brob, bmask,
               pDivV, pDivRob, pDivMask, pDivCnt, (pDivMask & bmask) ? 1 : 0);
      // Mask rewritten without a resolution naming it -> clobber suspicion.
      if (pDivV && divV && divRob == pDivRob && divMask != pDivMask)
        printf("[cyc %llu] DIV-MASK   rob=%u 0x%x -> 0x%x  (branchEvals v=%u "
               "mask=0x%x)\n",
               (unsigned long long)cyc, divRob, pDivMask, divMask, bev, bmask);
      if (pDivV && !divV)
        printf("[cyc %llu] DIV-CLEAR  rob=%u mask=0x%x cnt=%u\n",
               (unsigned long long)cyc, pDivRob, pDivMask, pDivCnt);
    }

    pRespV = respV; pRespRob = respRob;
    pServV = servV; pServRob = servRob; pServMask = servMask;
    pDivV = divV; pDivRob = divRob; pDivMask = divMask; pDivCnt = divCnt;
    pReqV = reqV; pReqRob = reqRob; pReqMask = reqMask; pReqInsn = reqInsn;
    (void)pReqV;

    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
  }

  printf("\n=== M-ext response attribution over %llu cycles (hart1) ===\n",
         (unsigned long long)cyc);
  printf("  driven by MULTIPLY pipeline : %llu\n", (unsigned long long)nMulDrove);
  printf("  driven by DIVIDER           : %llu\n", (unsigned long long)nDivDrove);
  printf("  NEITHER driver live (!)     : %llu\n", (unsigned long long)nNeither);
  return 0;
}
