// lrsc_wedge_probe6.cpp — dual-core (c1+c3) cycle trace around the fatal
// release-store kill in mt-lrscirq: every lookup op on both cores, plus each
// core's set-0x36 tag/owner view and c1's ACE/replay state, to show how c1
// keeps a stale valid copy of the owner line across c3's ReadUnique
// invalidation and then CleanUnique-upgrades it (resurrecting owner!=0).
//
// Build:  make build/lrsc_wedge_probe6.out
// Run  :  LRSCP_FROM=.. LRSCP_END=.. \
//         build/lrsc_wedge_probe6.out bins/mt-lrscirq-short-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CORE(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__##sig

static const char *opname(uint32_t insn, int wd) {
  uint32_t opc = insn & 0x7f;
  if (opc == 0x2f) {
    uint32_t f5 = insn >> 27;
    if (f5 == 0x02) return wd ? "LR.w " : "LR.r ";
    if (f5 == 0x03) return wd ? "SC.w " : "SC.r ";
    return wd ? "AMO.w" : "AMO.r";
  }
  if (opc == 0x03) return "LD   ";
  if (opc == 0x23) return "ST   ";
  return "??   ";
}

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-lrscirq-short-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("LRSCP_END",  3800800ULL);
  const uint64_t SNAP_FROM = env64("LRSCP_FROM", 3798300ULL);

  const int SET = (0xd80 >> 6) & 0x7f;

  uint64_t cyc = 0;
  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < SNAP_FROM) continue;

#define OPLINE(n)                                                              \
    if (CORE(n, cacheLookup__DOT__readBuffer_valid)) {                         \
      uint32_t insn = (uint32_t)CORE(n, cacheLookup__DOT__readBuffer_core_instruction); \
      int wd = (int)CORE(n, cacheLookup__DOT__readBuffer_writeData_valid);     \
      printf("%8llu c%d %s a=%05llx rt=%d rsp=%d wr=%llx\n",                   \
             (unsigned long long)cyc, n, opname(insn, wd),                     \
             (unsigned long long)(CORE(n, cacheLookup__DOT__readBuffer_address) & 0xfffff), \
             (int)CORE(n, cacheLookup__DOT__requestType),                      \
             (int)CORE(n, cacheLookup__DOT__readBuffer_cacheLine_response),    \
             (unsigned long long)CORE(n, cacheLookup__DOT__readBuffer_writeData_data)); \
    }
    OPLINE(0); OPLINE(1); OPLINE(2); OPLINE(3);

    // every 8 cycles: both cores' set-0x36 view + c1 ACE/replay pipeline state
    if ((cyc & 7) == 0) {
#define VIEW(n)                                                                \
      do {                                                                     \
        uint32_t *tw = &CORE(n, cacheLookup__DOT__tagBRAM__DOT__mem)[SET][0];  \
        unsigned __int128 te = ((unsigned __int128)tw[2] << 64) |              \
                               ((unsigned __int128)tw[1] << 32) | tw[0];       \
        uint32_t *d0 = &CORE(n, cacheLookup__DOT__dataBRAM_0__DOT__mem)[SET][0]; \
        uint64_t t0 = (uint64_t)(te & 0x7fffff);                               \
        printf("   . c%d w0=%c%c%c o=%llx cs=%llx | aceRd=%d aceRs=%d rdBuf=%d coh=%d snpV=%d snpA=%05llx snpR=%d hold=%d mr=%d\n", \
               n,                                                              \
               ((t0 >> 19) & 1) ? 'V' : '-', ((t0 >> 21) & 1) ? 'S' : '-',     \
               ((t0 >> 20) & 1) ? 'D' : '-',                                   \
               (unsigned long long)(((uint64_t)d0[1] << 32) | d0[0]),          \
               (unsigned long long)(((uint64_t)d0[3] << 32) | d0[2]),          \
               (int)CORE(n, aceUnit__DOT__readACERequestState),                \
               (int)CORE(n, aceUnit__DOT__readACEResponseState),               \
               (int)CORE(n, aceUnit__DOT__readBuffer_valid),                   \
               (int)CORE(n, aceUnit__DOT__coherentAXIState),                   \
               (int)CORE(n, aceUnit__DOT__coherencyRequestBuffer_valid),       \
               (unsigned long long)(CORE(n, aceUnit__DOT__coherencyRequestBuffer_address) & 0xfffff), \
               (int)CORE(n, aceUnit__DOT__coherencyRequestBuffer_response),    \
               (int)CORE(n, aceUnit__DOT__toCoherentRequestInStateWire),       \
               (int)CORE(n, cacheLookup__DOT__lastInorderMissRecordRegister_valid)); \
      } while (0)
      VIEW(0); VIEW(1); VIEW(2); VIEW(3);
    }
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
