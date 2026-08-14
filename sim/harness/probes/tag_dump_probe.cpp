// tag_dump_probe.cpp — boot linux-q4 RTL-only; at fixed cycles around the
// stale-task-pointer ReadShared (cyc 59,447,189 on the current model) dump
// hart3's and hart0's L1-D tag word + all 4 ways' data dwords for the set
// holding line 0x80c2fc40 (set 113, tag 0x40617). Split-brain check: two
// ways valid with the same tag, or the missing store's value hiding in a
// non-served way.
//
// Build:  make build/tag_dump_probe.out
// Run  :  build/tag_dump_probe.out bins/linux-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include "sim/rtl/rtl_model.h"

static const int SET = 113;          // (0x80c2fc40 >> 6) & 0x7f
static const uint32_t TAG = 0x40617; // 0x80c2fc40 >> 13, 19 bits

#define TAGMEM(n) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__tagBRAM__DOT__mem
#define DATAMEM(n, w) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__dataBRAM_##w##__DOT__mem

// 92-bit tag word: way i at bits [23i+22 : 23i], layout PLRU|Share|Dirty|Valid|tag19
static void dump_tag(const char *who, const uint32_t *w) {
  // w[0..2] little-endian 32-bit words
  for (int i = 0; i < 4; ++i) {
    int lo = 23 * i;
    uint64_t chunk = 0;
    for (int b = 0; b < 23; ++b) {
      int bit = lo + b;
      if ((w[bit / 32] >> (bit % 32)) & 1) chunk |= (1ULL << b);
    }
    uint32_t tag = chunk & 0x7ffff;
    printf("[tag] %s way%d V=%d D=%d S=%d P=%d tag=0x%05x %s\n", who, i,
           (int)((chunk >> 19) & 1), (int)((chunk >> 20) & 1),
           (int)((chunk >> 21) & 1), (int)((chunk >> 22) & 1), tag,
           (tag == TAG && ((chunk >> 19) & 1)) ? "<== MATCH" : "");
  }
}

// 512-bit line: dwords 0..7; print dwords 0 and 1 (0x40 and 0x48 offsets)
static void dump_data(const char *who, int way, const uint32_t *w) {
  uint64_t d0 = ((uint64_t)w[1] << 32) | w[0];
  uint64_t d1 = ((uint64_t)w[3] << 32) | w[2];
  printf("[dat] %s way%d dw0=0x%016llx dw1(taskptr)=0x%016llx\n", who, way,
         (unsigned long long)d0, (unsigned long long)d1);
}

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  const uint64_t SNAPS[] = {59440000ULL, 59447150ULL, 59447500ULL, 59460000ULL};
  int snap_i = 0;

  uint64_t cyc = 0;
  while (cyc < 59500000ULL) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if ((cyc % 10000000ULL) == 0) {
      printf("[tag] progress cyc=%llu\n", (unsigned long long)cyc);
      fflush(stdout);
    }
    if (snap_i < 4 && cyc == SNAPS[snap_i]) {
      ++snap_i;
      printf("[tag] ==== SNAP cyc=%llu set=%d ====\n", (unsigned long long)cyc, SET);
      dump_tag("h3", (const uint32_t *)TAGMEM(3)[SET]);
      dump_data("h3", 0, (const uint32_t *)DATAMEM(3, 0)[SET]);
      dump_data("h3", 1, (const uint32_t *)DATAMEM(3, 1)[SET]);
      dump_data("h3", 2, (const uint32_t *)DATAMEM(3, 2)[SET]);
      dump_data("h3", 3, (const uint32_t *)DATAMEM(3, 3)[SET]);
      dump_tag("h0", (const uint32_t *)TAGMEM(0)[SET]);
      dump_data("h0", 0, (const uint32_t *)DATAMEM(0, 0)[SET]);
      dump_data("h0", 1, (const uint32_t *)DATAMEM(0, 1)[SET]);
      dump_data("h0", 2, (const uint32_t *)DATAMEM(0, 2)[SET]);
      dump_data("h0", 3, (const uint32_t *)DATAMEM(0, 3)[SET]);
      fflush(stdout);
    }
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
