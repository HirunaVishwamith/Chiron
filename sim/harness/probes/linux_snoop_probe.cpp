// linux_snoop_probe — is the snoop answered from the STALE writeback copy?
//
// The lost store is hart0's `sw zero,8(s3)` committing at cycle 871254973, on
// line 0x87dd6b80. The theory to test: hart1's polling load snoops hart0, and
// ACEUnit answers it out of the writeback pipeline
// (chooseFromWriteBufferWire := writePipeHit) using the copy the fence.i
// walker captured BEFORE the store — handing the peer stale 0x11 with
// PassDirty and losing the committed 0 permanently.
//
// Every C-level repro (mt-fencei in three shapes, mt-llist, mt-crosscall)
// failed to reproduce the bug, including with a 512-line dirty working set on
// both the pre-fix and fixed RTL. So stop inferring the mechanism from
// programs and watch the snoop machinery directly, on the PRE-FIX model where
// the bug is live and the checkpoint ladder is still valid.
//
// Prints, for the watched line only: each snoop request presented to hart0,
// whether writePipeHit steered the answer into the writeback pipe, and the
// flags word actually returned. The line's byte offset 8 is csd->node.u_flags,
// which is word index 2 of the 512-bit cacheLine — 0x11 = STALE (bug
// confirmed), 0x00 = the fresh value (theory refuted).
//
// Run (pre-fix model only):
//   make linux_snoop_probe.out
//   CKPT_RESTORE=ckpt/ckpt_000850000000.bin ./build/linux_snoop_probe.out

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

#define ACE_(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__memAccess__DOT__aceUnit__DOT__##s
// SN_HART selects which hart's ACE unit to watch. Verilator flattens the
// hierarchy, so each core is a distinct member and cannot be indexed.
#define ACE(N, s) (HART == 1 ? (uint64_t)ACE_(1, s) : HART == 2 ? (uint64_t)ACE_(2, s) \
                 : HART == 3 ? (uint64_t)ACE_(3, s) : (uint64_t)ACE_(0, s))
#define ACELINE(N) (HART == 1 ? (const uint32_t *)ACE_(1, coherencyResponseBuffer_cacheLine) \
                  : HART == 2 ? (const uint32_t *)ACE_(2, coherencyResponseBuffer_cacheLine) \
                  : HART == 3 ? (const uint32_t *)ACE_(3, coherencyResponseBuffer_cacheLine) \
                              : (const uint32_t *)ACE_(0, coherencyResponseBuffer_cacheLine))

int main() {
  const char *ckpt = getenv("CKPT_RESTORE");
  const uint64_t ADDR = getenv("SN_ADDR") ? strtoull(getenv("SN_ADDR"), 0, 0)
                                          : 0x87dd6b80ULL;
  const uint64_t FROM = getenv("SN_FROM") ? strtoull(getenv("SN_FROM"), 0, 0)
                                          : 871238000ULL;
  const uint64_t TO   = getenv("SN_TO")   ? strtoull(getenv("SN_TO"), 0, 0)
                                          : 871280000ULL;
  const int HART = getenv("SN_HART") ? atoi(getenv("SN_HART")) : 0;
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }

  uint64_t cyc = 0, m[4] = {}, lu = 0, ub = 0;
  simulator bench;
  bench.init_no_image();
  Vsystem *tb = bench.raw();
  {
    VerilatedRestore rs;
    rs.open(ckpt);
    if (!rs.isOpen()) { std::fprintf(stderr, "cannot open %s\n", ckpt); return 1; }
    rs >> cyc; rs >> m[0]; rs >> m[1]; rs >> m[2]; rs >> m[3];
    rs >> lu; rs >> ub; rs >> *tb; rs.close();
  }

  const uint64_t line = (ADDR & ~63ULL) & 0xffffffffULL;
  std::printf("restored at %" PRIu64 ", watching line %08" PRIx64 " on hart%d\n",
              cyc, line, HART);
  std::printf("window %" PRIu64 " .. %" PRIu64 "\n", FROM, TO);
  std::printf("(flagsWord 00000011 = STALE copy served; 00000000 = fresh)\n\n");
  std::fflush(stdout);

  const bool all_snoops = getenv("SN_ALL") != nullptr;
  uint64_t other_snoops = 0, last_other = ~0ULL;
  int prev_req = -1, prev_hit = -1;
  for (;;) {
    tb->eval();
    if (cyc >= FROM && cyc < TO) {
      const uint64_t sa = (uint64_t)ACE(0, coherencyRequestBuffer_address) & ~63ULL;
      // Control: SN_ALL=1 reports EVERY snoop reaching this hart, not just the
      // watched line. "No snoop for our line" is only meaningful if other
      // snoops are visibly arriving — otherwise the probe is simply blind.
      if (all_snoops && (int)ACE(0, coherencyRequestBuffer_valid) && sa != line) {
        if (sa != last_other) {
          other_snoops++;
          last_other = sa;
          if (other_snoops <= 20)
            std::printf("%12" PRIu64 "  (other snoop) line=%08" PRIx64 "\n", cyc, sa);
        }
      }
      if (sa == line) {
        const int req = (int)ACE(0, coherencyRequestBuffer_valid);
        const int hit = (int)ACE(0, writePipeHit);
        if (req != prev_req || hit != prev_hit) {
          const uint32_t *cl = ACELINE(0);
          std::printf("%12" PRIu64 "  snoopReq=%d writePipeHit=%d  respValid=%d"
                      " dataValid=%d resp=%u  flagsWord=%08x%s\n",
                      cyc, req, hit,
                      (int)ACE(0, coherencyResponseBuffer_valid),
                      (int)ACE(0, coherencyResponseBuffer_dataValid),
                      (unsigned)ACE(0, coherencyResponseBuffer_response),
                      cl[2],
                      (hit && cl[2] == 0x11u) ? "   <== STALE FROM PIPE" : "");
          std::fflush(stdout);
          prev_req = req; prev_hit = hit;
        }
      }
    }
    if (cyc >= TO) break;
    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    cyc++;
    if (cyc % 5000000 == 0) {
      std::fprintf(stderr, "  [%" PRIu64 "]\n", cyc);
      std::fflush(stderr);
    }
  }
  std::printf("\nsnoops to OTHER lines seen at hart0 in window: %" PRIu64 "\n",
              other_snoops);
  std::printf("done at %" PRIu64 "\n", cyc);
  return 0;
}
