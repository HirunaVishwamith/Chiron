// lrsc_wedge_probe5.cpp — coherency-view dump of the mt-lrscirq owner line
// (0x80000d80: owner@+0, count@+8, sense@+0xc — one false-shared line): for
// every core, all 4 ways of set 0x36 with V/S/D flags and the line's owner
// dword + count/sense dword, plus the DRAM copy. Shows which agent serves the
// stale nonzero owner that makes both spinning cores read "lock held" forever.
//
// Build:  make build/lrsc_wedge_probe5.out
// Run  :  LRSCP_FROM=.. LRSCP_STEP=.. LRSCP_END=.. \
//         build/lrsc_wedge_probe5.out bins/mt-lrscirq-short-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

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
  const uint64_t END       = env64("LRSCP_END",  10020000ULL);
  const uint64_t SNAP_FROM = env64("LRSCP_FROM", 10000000ULL);
  const uint64_t SNAP_STEP = env64("LRSCP_STEP",    200ULL);

  const int SET = (0xd80 >> 6) & 0x7f;   // L1 set of the owner line

#define PERCORE(n)                                                              \
  do {                                                                          \
    uint32_t *tw = &tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__tagBRAM__DOT__mem[SET][0]; \
    unsigned __int128 te = ((unsigned __int128)tw[2] << 64) |                   \
                           ((unsigned __int128)tw[1] << 32) | tw[0];            \
    printf("  c%d", n);                                                         \
    uint32_t *d;                                                                \
    for (int wy = 0; wy < 4; wy++) {                                            \
      uint64_t t = (uint64_t)((te >> (23 * wy)) & 0x7fffff);                    \
      switch (wy) {                                                             \
      case 0: d = &tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_0__DOT__mem[SET][0]; break; \
      case 1: d = &tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_1__DOT__mem[SET][0]; break; \
      case 2: d = &tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_2__DOT__mem[SET][0]; break; \
      default:d = &tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_3__DOT__mem[SET][0]; break; \
      }                                                                         \
      uint64_t own = ((uint64_t)d[1] << 32) | d[0];                             \
      uint64_t cs  = ((uint64_t)d[3] << 32) | d[2];                             \
      printf(" w%d[%c%c%c t%05llx o=%llx cs=%llx]", wy,                         \
             ((t >> 19) & 1) ? 'V' : '-', ((t >> 21) & 1) ? 'S' : '-',          \
             ((t >> 20) & 1) ? 'D' : '-', (unsigned long long)(t & 0x7ffff),    \
             (unsigned long long)own, (unsigned long long)cs);                  \
    }                                                                           \
    printf("\n");                                                               \
  } while (0)

  uint64_t cyc = 0, next_snap = SNAP_FROM;
  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < next_snap) continue;
    next_snap += SNAP_STEP;

    printf("[snap %8llu] pcs=%llx/%llx/%llx/%llx dram_o=%llx dram_cs=%llx\n",
           (unsigned long long)cyc,
           (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
           (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc,
           (unsigned long long)bench.read_dram64(0x80000d80ULL),
           (unsigned long long)bench.read_dram64(0x80000d88ULL));
    PERCORE(0); PERCORE(1); PERCORE(2); PERCORE(3);
    fflush(stdout);
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
