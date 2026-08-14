// lrsc_wedge_probe7.cpp — line-state change tracker for the mt-lrscirq owner
// line (set 0x36): every cycle, if any core's way-0..3 tag state (V/S/D) or
// the line's owner/count-sense data changed, print one compact line. Makes
// the dirty-data handoff chain (and the exact drop/stale-fill moment)
// directly greppable. Also flags STALE-FILL: an invalid->valid transition
// whose count/sense dword differs from the last value held by any valid copy.
//
// Build:  make build/lrsc_wedge_probe7.out
// Run  :  LRSCP_FROM=.. LRSCP_END=.. \
//         build/lrsc_wedge_probe7.out bins/mt-lrscirq-short-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CORE_T(n) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__tagBRAM__DOT__mem
#define CORE_D(n, w) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_##w##__DOT__mem

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
  const uint64_t END       = env64("LRSCP_END",  2480000ULL);
  const uint64_t SNAP_FROM = env64("LRSCP_FROM", 2476000ULL);
  const int SET = (0xd80 >> 6) & 0x7f;

  // per core, per way: {flags(3b), owner, cs}
  struct WaySt { int fl; uint64_t o, cs; };
  WaySt prev[4][4] = {};
  uint64_t lastValidCs = ~0ULL, lastValidO = ~0ULL;
  bool anyValidPrev = false;

  auto getWay = [&](int c, int w, WaySt &out) {
    uint32_t *tw, *dw;
    switch (c) {
    case 0: tw = &CORE_T(0)[SET][0]; break;
    case 1: tw = &CORE_T(1)[SET][0]; break;
    case 2: tw = &CORE_T(2)[SET][0]; break;
    default: tw = &CORE_T(3)[SET][0]; break;
    }
    unsigned __int128 te = ((unsigned __int128)tw[2] << 64) |
                           ((unsigned __int128)tw[1] << 32) | tw[0];
    uint64_t t = (uint64_t)((te >> (23 * w)) & 0x7fffff);
    switch (c * 4 + w) {
    case 0:  dw = &CORE_D(0, 0)[SET][0]; break;
    case 1:  dw = &CORE_D(0, 1)[SET][0]; break;
    case 2:  dw = &CORE_D(0, 2)[SET][0]; break;
    case 3:  dw = &CORE_D(0, 3)[SET][0]; break;
    case 4:  dw = &CORE_D(1, 0)[SET][0]; break;
    case 5:  dw = &CORE_D(1, 1)[SET][0]; break;
    case 6:  dw = &CORE_D(1, 2)[SET][0]; break;
    case 7:  dw = &CORE_D(1, 3)[SET][0]; break;
    case 8:  dw = &CORE_D(2, 0)[SET][0]; break;
    case 9:  dw = &CORE_D(2, 1)[SET][0]; break;
    case 10: dw = &CORE_D(2, 2)[SET][0]; break;
    case 11: dw = &CORE_D(2, 3)[SET][0]; break;
    case 12: dw = &CORE_D(3, 0)[SET][0]; break;
    case 13: dw = &CORE_D(3, 1)[SET][0]; break;
    case 14: dw = &CORE_D(3, 2)[SET][0]; break;
    default: dw = &CORE_D(3, 3)[SET][0]; break;
    }
    out.fl = (int)((t >> 19) & 7);  // {S? no: bits 19=V,20=D,21=S} -> V=1,D=2,S=4
    out.o  = ((uint64_t)dw[1] << 32) | dw[0];
    out.cs = ((uint64_t)dw[3] << 32) | dw[2];
  };

  uint64_t cyc = 0;
  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < SNAP_FROM) continue;

    bool anyValidNow = false;
    for (int c = 0; c < 4; c++)
      for (int w = 0; w < 4; w++) {
        WaySt s; getWay(c, w, s);
        if (s.fl != prev[c][w].fl || s.o != prev[c][w].o || s.cs != prev[c][w].cs) {
          bool nowValid = s.fl & 1, wasValid = prev[c][w].fl & 1;
          const char *tag = "";
          if (nowValid && !wasValid && lastValidCs != ~0ULL && s.cs != lastValidCs)
            tag = "  <<< STALE-FILL (cs)";
          printf("%8llu c%dw%d %c%c%c o=%llx cs=%llx  (was %c%c%c o=%llx cs=%llx)%s\n",
                 (unsigned long long)cyc, c, w,
                 nowValid ? 'V' : '-', (s.fl & 4) ? 'S' : '-', (s.fl & 2) ? 'D' : '-',
                 (unsigned long long)s.o, (unsigned long long)s.cs,
                 wasValid ? 'V' : '-', (prev[c][w].fl & 4) ? 'S' : '-', (prev[c][w].fl & 2) ? 'D' : '-',
                 (unsigned long long)prev[c][w].o, (unsigned long long)prev[c][w].cs, tag);
          prev[c][w] = s;
        }
        if (s.fl & 1) { anyValidNow = true; lastValidCs = s.cs; lastValidO = s.o; }
      }
    if (anyValidPrev && !anyValidNow)
      printf("%8llu ALL-INVALID lastValid o=%llx cs=%llx\n", (unsigned long long)cyc,
             (unsigned long long)lastValidO, (unsigned long long)lastValidCs);
    anyValidPrev = anyValidNow;
    if ((cyc % 1000000ULL) == 0) fflush(stdout);
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
