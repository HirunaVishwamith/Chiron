// w3path_probe.cpp — which of the two response paths delivers the stale load?
//
// prfwrite_probe named the culprit exactly:
//   [cyc 9067568] PRF[33] 3 -> 290   map(14)=33
//       w3 LOAD  valid=1 dest=33 insn=18063783 result=290
// 0x18063783 decodes as `ld a5,384(a2)` — architectural rd is a5, yet it writes
// p33, which is a4's committed mapping. So this is a squashed/stale load whose
// D-cache request outlived it and landed in a reallocated physical register.
//
// core.scala:837 puts NO squash check on the w3 write port (the only gate is
// rd != 0), so the drop must happen inside cacheModule. There:
//   cacheModule.scala:160-161
//     responseOut.valid := Mux(cacheLookup.toResponse.request.valid,
//                              cacheLookup.toResponse.request.valid &&
//                              cacheLookup.toResponse.request.branch.valid,
//                              peripheralUnit.responseOut.request.valid)
// The cache path IS gated on branch.valid; the peripheral (MMIO) path is NOT.
// So the two candidate faults are:
//   (P) the response came from peripheralUnit and slipped through ungated; or
//   (C) it came from cacheLookup with branch.valid still 1, i.e. the squash
//       never reached the request (the ACEUnit.scala:301-305 / regReadUpdate
//       ambiguity, where branch.valid=0 cannot distinguish "squashed" from
//       "never speculative").
// They need different fixes, so this probe discriminates before anything is
// changed. It reports the selector and both sources on every w3 write to WATCH.
//
// Build: make build/w3path_probe.out
// Run  : build/w3path_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig) tb_->system__DOT__chiron__DOT__core0__DOT__##sig
#define MA(sig) tb_->system__DOT__chiron__DOT__core0__DOT__memAccess__DOT__##sig

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

  uint64_t cyc = 0;

  while (cyc < STOP) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < FROM) continue;

    const unsigned w3v = (unsigned)C0(memAccess_responseOut_valid);
    const unsigned w3d = (unsigned)C0(memAccess_responseOut_prfDest);
    if (!(w3v && w3d == WATCH)) continue;

    // The Mux selector at cacheModule.scala:160 is cacheLookup's own valid.
    const unsigned clv = (unsigned)MA(cacheLookup_toResponse_request_valid);
    const unsigned pv  = (unsigned)MA(peripheralUnit__DOT__responseOutBuffer_valid);
    const unsigned pd  = (unsigned)MA(peripheralUnit__DOT__responseOutBuffer_core_prfDest);
    const uint64_t pi  = (uint64_t)MA(peripheralUnit__DOT__responseOutBuffer_core_instruction);
    const unsigned pr  = (unsigned)MA(peripheralUnit__DOT__responseOutBuffer_core_robAddr);
    const uint64_t pdt = (uint64_t)MA(peripheralUnit__DOT__responseOutBuffer_writeData_data);

    printf("[cyc %llu] w3 -> p%u  insn=%08llx result=%llx\n",
           (unsigned long long)cyc, WATCH,
           (unsigned long long)C0(memAccess_responseOut_instruction),
           (unsigned long long)C0(memAccess_responseOut_result));
    printf("    selector cacheLookup.toResponse.valid = %u  => path %s\n",
           clv, clv ? "C (cache, branch.valid-gated)"
                    : "P (peripheral, UNGATED)");
    printf("    peripheral buf: valid=%u dest=%u rob=%u insn=%08llx data=%llx\n",
           pv, pd, pr, (unsigned long long)pi, (unsigned long long)pdt);
  }
  printf("\ndone cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
