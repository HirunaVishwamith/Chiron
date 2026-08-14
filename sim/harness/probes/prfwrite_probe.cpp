// prfwrite_probe.cpp — name the write port that destroys a4 in mt-ipitmr.
//
// Everything upstream of this is settled by measurement:
//   * a4 == PRF[architecturalRegMap(14)] and map(14) is UNCHANGED at 33 across
//     the corruption, so it is not a bogus rename.
//   * PRFFreeList_33 / reservedFreeList1_33 are 0 throughout: p33 was never
//     returned to the free list, so it is not a premature release.
//   * dualalloc_probe found 0 re-issues of p33 while owned, so it is not a
//     rename double-allocation either.
// => something writes PRF[33] while p33 is legitimately a4's committed mapping.
//
// PRF.scala:146 has four write ports, and system.v:51236-51247 shows exactly
// what drives them:
//   w1 = singleCycleArithmeticResponse   (ALU)
//   w2 = decode.jumpAddrWrite            (JAL/JALR link address)
//   w3 = memAccess.responseOut           (D-cache / ACE load return)
//   w4 = extnMResponse                   (M-extension mul/div)
// All four source bundles survive Verilator's optimiser even though the
// prf_w*_addr/en nets themselves are inlined, so the port can be identified
// from its source. This probe watches physicalRegisterFile_33 directly and,
// on every change, prints which ports were presenting dest 33 that cycle
// together with their payloads and the branch/injection state.
//
// A w3 hit means the stale-ACE-MSHR-writeback theory is right (a squashed
// load's refill lands in a reallocated register). A w1/w4 hit means a
// squashed ALU/M-unit result is being written back without a squash check.
//
// Build: make build/prfwrite_probe.out
// Run  : build/prfwrite_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig) tb_->system__DOT__chiron__DOT__core0__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t STOP  = env64("STOP", 9067800ULL);
  const uint64_t FROM  = env64("FROM", 9066000ULL);
  const unsigned WATCH = (unsigned)env64("WATCH", 33ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0, prev_p = 0;
  bool first = true;

  while (cyc < STOP) {
    tb_->eval();

    // Sample the write ports as they stand at the posedge, i.e. before the
    // clock is driven high — these are what the nonblocking assign will use.
    const unsigned w1v = (unsigned)C0(singleCycleArithmeticResponse_valid);
    const unsigned w1d = (unsigned)C0(singleCycleArithmeticResponse_prfDest);
    const uint64_t w1r = (uint64_t)C0(singleCycleArithmeticResponse_result);
    const unsigned w1b = (unsigned)C0(singleCycleArithmeticResponse_robAddr);
    const unsigned w1e = (unsigned)C0(prf_w1_en);

    const unsigned w2v = (unsigned)C0(decode_jumpAddrWrite_ready);
    const uint64_t w2r = (uint64_t)C0(decode_jumpAddrWrite_linkAddr);

    const unsigned w3v = (unsigned)C0(memAccess_responseOut_valid);
    const unsigned w3d = (unsigned)C0(memAccess_responseOut_prfDest);
    const uint64_t w3r = (uint64_t)C0(memAccess_responseOut_result);
    const uint64_t w3i = (uint64_t)C0(memAccess_responseOut_instruction);

    const unsigned w4v = (unsigned)C0(extnMResponse_valid);
    const unsigned w4d = (unsigned)C0(extnMResponse_prfDest);
    const uint64_t w4r = (uint64_t)C0(extnMResponse_result);
    const unsigned w4b = (unsigned)C0(extnMResponse_robAddr);

    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const uint64_t p = (uint64_t)C0(prf__DOT__physicalRegisterFile_33);
    if (!first && p != prev_p && cyc >= FROM) {
      printf("[cyc %llu] PRF[%u] %llx -> %llx   map(14)=%u free33=%u  "
             "beV=%u beP=%u brMask=%x inj=%u  headPC=%llx\n",
             (unsigned long long)cyc, WATCH,
             (unsigned long long)prev_p, (unsigned long long)p,
             (unsigned)C0(decode__DOT__architecturalRegMap_14),
             (unsigned)C0(decode__DOT__PRFFreeList_33),
             (unsigned)C0(branchEvals_valid), (unsigned)C0(branchEvals_passed),
             (unsigned)C0(branchEvals_branchMask),
             (unsigned)C0(interruptInjectStatus),
             (unsigned long long)tb_->robOut0_pc);
    // Attribute the write. Ports whose dest is 33 and which were valid at the
    // edge are the candidates; normally exactly one matches.
      if (w1v && w1d == WATCH)
        printf("    w1 ALU      valid=%u en=%u dest=%u rob=%u result=%llx\n",
               w1v, w1e, w1d, w1b, (unsigned long long)w1r);
      if (w2v)
        printf("    w2 JAL-link ready=%u linkAddr=%llx  (dest not observable)\n",
               w2v, (unsigned long long)w2r);
      if (w3v && w3d == WATCH)
        printf("    w3 LOAD     valid=%u dest=%u insn=%08llx result=%llx"
               "   <== ACE/D-cache return\n",
               w3v, w3d, (unsigned long long)w3i, (unsigned long long)w3r);
      if (w4v && w4d == WATCH)
        printf("    w4 MULDIV   valid=%u dest=%u rob=%u result=%llx\n",
               w4v, w4d, w4b, (unsigned long long)w4r);
      if (!(w1v && w1d == WATCH) && !w2v && !(w3v && w3d == WATCH) &&
          !(w4v && w4d == WATCH))
        printf("    !! no port presented dest %u at the edge "
               "(w1 %u/%u, w3 %u/%u, w4 %u/%u)\n",
               WATCH, w1v, w1d, w3v, w3d, w4v, w4d);
    }
    prev_p = p; first = false;
  }
  printf("\ndone cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
