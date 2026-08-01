// periph_squash_probe.cpp — is the MMIO (peripheral) response path leaking
// squashed loads into the PRF?
//
// cacheModule.scala:160-161 gates the CACHE response arm on branch.valid but
// forwards the PERIPHERAL arm unconditionally:
//
//   responseOut.valid := Mux(cacheLookup.toResponse.request.valid,
//                            cacheLookup.toResponse.request.valid &&
//                              cacheLookup.toResponse.request.branch.valid,
//                            peripheralUnit.responseOut.request.valid)
//
// and peripheralUnit.scala never applies branchOps to any of its buffers
// (requestBuffer / readRequestBuffer / writeRequestBuffer / peripheralMSHR /
// responseOutBuffer) — grep shows only the two regWriteUpdate calls at
// enqueue, no regRecordUpdate ageing anywhere. So once an MMIO request is
// accepted its branch state is frozen at "live" forever.
//
// That matters because of core.scala:618:
//   coherentLoadInvalid = a committing LOAD whose line was invalidated
// which raises branchEvals with branchMask = coherent_BranchMask (0x10,
// configuration.scala:15), passed = 0, plus rob.flushAll (core.scala:806).
// Every in-flight memory op carries bit 4, so that event squashes the whole
// pipeline — except anything already inside the peripheral unit, which
// completes anyway and writes core.scala:837's ungated w3 PRF port. By then
// the flush has rolled back the rename map and the destination physical
// register belongs to someone else. Identical shape to the ACE
// responseBuffer bug (see memory: ace-responsebuffer-squash-clobber).
//
// This probe measures whether the window is actually entered:
//   SQUASH-IN-FLIGHT : a squashing branchEval (passed=0) lands while this
//                      core has an MMIO read in flight anywhere in the
//                      peripheral unit.
//   LATE-RESPONSE    : a peripheral response is subsequently handed to
//                      memAccess.responseOut for a request that was in
//                      flight across such a squash.
// The second is the one that corrupts state; both are printed with context.
//
// Build: make build/periph_squash_probe.out
// Run  : build/periph_squash_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CORE(n, s) tb_->system__DOT__chiron__DOT__core##n##__DOT__##s
#define PU(n, s)   CORE(n, memAccess__DOT__peripheralUnit__DOT__##s)

struct Stat {
  uint64_t squash_inflight = 0;
  uint64_t late_response   = 0;
  uint64_t printed         = 0;
  // set when a squash lands while a read is in flight; cleared when the
  // peripheral unit goes fully idle again.
  bool     tainted         = false;
  uint64_t taint_cyc       = 0;
  unsigned taint_mask      = 0;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END      = env64("END", 5000000ULL);
  const uint64_t MAXPRINT = env64("MAXPRINT", 8ULL);
  // Only count squashes carrying the coherency bit unless COHERENT_ONLY=0.
  const uint64_t COH_ONLY = env64("COHERENT_ONLY", 0ULL);

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
      /* anything of ours still moving through the peripheral unit */         \
      const unsigned inflight =                                               \
          (unsigned)PU(n, requestBuffer_valid) |                              \
          (unsigned)PU(n, readRequestBuffer_valid) |                          \
          (unsigned)PU(n, writeRequestBuffer_valid) |                         \
          (unsigned)(!PU(n, peripheralMSHR__DOT__emptyReg)) |                 \
          (unsigned)PU(n, responseOutBuffer_valid) |                          \
          (unsigned)(PU(n, readAXIRequestState) != 0) |                       \
          (unsigned)(PU(n, readAXIResponseState) != 0);                       \
      const unsigned bev  = (unsigned)CORE(n, branchEvals_valid);             \
      const unsigned bpas = (unsigned)CORE(n, branchEvals_passed);            \
      const unsigned bmsk = (unsigned)CORE(n, branchEvals_branchMask);        \
      if (bev && !bpas && inflight && (!COH_ONLY || (bmsk & 0x10))) {         \
        st[n].squash_inflight++;                                              \
        if (!st[n].tainted) {                                                 \
          st[n].tainted = true; st[n].taint_cyc = cyc; st[n].taint_mask = bmsk;\
        }                                                                     \
        if (st[n].printed < MAXPRINT) {                                       \
          st[n].printed++;                                                    \
          printf("[cyc %llu] core%d SQUASH(mask=%02x,%s) while MMIO in "      \
                 "flight: reqBuf{v=%u bv=%u insn=%08x} rdBuf{v=%u bv=%u "     \
                 "insn=%08x prf=%u} mshrEmpty=%u respBuf{v=%u insn=%08x "     \
                 "prf=%u rob=%u} rdReqSt=%u rdRespSt=%u\n",                   \
                 (unsigned long long)cyc, n, bmsk,                            \
                 (bmsk & 0x10) ? "coherent" : "branch",                       \
                 (unsigned)PU(n, requestBuffer_valid),                        \
                 (unsigned)PU(n, requestBuffer_branch_valid),                 \
                 (unsigned)PU(n, requestBuffer_core_instruction),             \
                 (unsigned)PU(n, readRequestBuffer_valid),                    \
                 (unsigned)PU(n, readRequestBuffer_branch_valid),             \
                 (unsigned)PU(n, readRequestBuffer_core_instruction),         \
                 (unsigned)PU(n, readRequestBuffer_core_prfDest),             \
                 (unsigned)PU(n, peripheralMSHR__DOT__emptyReg),              \
                 (unsigned)PU(n, responseOutBuffer_valid),                    \
                 (unsigned)PU(n, responseOutBuffer_core_instruction),         \
                 (unsigned)PU(n, responseOutBuffer_core_prfDest),             \
                 (unsigned)PU(n, responseOutBuffer_core_robAddr),             \
                 (unsigned)PU(n, readAXIRequestState),                        \
                 (unsigned)PU(n, readAXIResponseState));                      \
        }                                                                     \
      }                                                                       \
      /* a peripheral response reaching the core after such a squash */       \
      if (st[n].tainted && (unsigned)PU(n, responseOutBuffer_valid) &&        \
          (unsigned)CORE(n, memAccess_responseOut_valid)) {                   \
        st[n].late_response++;                                                \
        if (st[n].printed < MAXPRINT) {                                       \
          st[n].printed++;                                                    \
          printf("[cyc %llu] core%d ** LATE MMIO RESPONSE after squash at "   \
                 "cyc %llu (mask=%02x): insn=%08x prf=%u rob=%u data=%llx "   \
                 "-> PRF WRITE\n",                                            \
                 (unsigned long long)cyc, n,                                  \
                 (unsigned long long)st[n].taint_cyc, st[n].taint_mask,       \
                 (unsigned)PU(n, responseOutBuffer_core_instruction),         \
                 (unsigned)PU(n, responseOutBuffer_core_prfDest),             \
                 (unsigned)PU(n, responseOutBuffer_core_robAddr),             \
                 (unsigned long long)PU(n, responseOutBuffer_writeData_data));\
        }                                                                     \
      }                                                                       \
      if (!inflight) st[n].tainted = false;                                   \
    } while (0)
    CHECK(0); CHECK(1); CHECK(2); CHECK(3);
#undef CHECK
  }

  printf("\n=== MMIO squash-leak summary over %llu cycles ===\n",
         (unsigned long long)cyc);
  for (int n = 0; n < 4; n++)
    printf("  core%d: squash-while-MMIO-in-flight=%llu  late-MMIO-response=%llu\n",
           n, (unsigned long long)st[n].squash_inflight,
           (unsigned long long)st[n].late_response);
  return 0;
}
