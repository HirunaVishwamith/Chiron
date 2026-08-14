// prf33_alloc_probe.cpp — is physical register 33 handed out to TWO instructions?
//
// Two explanations survive for the mt-ipitmr a4 corruption and they need
// different fixes:
//   (A) a SQUASHED load's ACE MSHR entry survives as a non-speculative entry
//       (ACEUnit.scala:301-305 enqueues it while regReadUpdate has just set
//       branch.valid=0/mask=0) and later writes p33, which by then is a4's
//       committed mapping; or
//   (B) p33 is DOUBLE-ALLOCATED — handed to the in-flight load AND to the
//       a4-writing instruction — in which case the MSHR entry is innocent and
//       the rename/free-list allocation is at fault.
//
// Discriminator: how many times is p33 handed out (decode.freeRegAddr == 33
// while its free bit drops) between the last time it was free and the
// corruption at cyc 9067755? One hand-out => (A). Two => (B).
//
// Build: make build/prf33_alloc_probe.out
// Run  : build/prf33_alloc_probe.out bins/mt-ipitmr-q4.bin
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
  const uint64_t STOP  = env64("STOP", 9067760ULL);
  const uint64_t FROM  = env64("FROM", 9067000ULL);
  const unsigned WATCH = (unsigned)env64("WATCH", 33ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  unsigned prev_free = ~0u, prev_fra = ~0u;
  int handouts = 0;

  while (cyc < STOP) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < FROM) {
      prev_free = (unsigned)C0(decode__DOT__PRFFreeList_33);
      prev_fra  = (unsigned)C0(decode__DOT__freeRegAddr);
      continue;
    }

    const unsigned f   = (unsigned)C0(decode__DOT__PRFFreeList_33);
    const unsigned fra = (unsigned)C0(decode__DOT__freeRegAddr);
    const unsigned m14 = (unsigned)C0(decode__DOT__architecturalRegMap_14);

    // freeRegAddr naming p33 is the rename picking it as a destination.
    if (fra == WATCH && fra != prev_fra) {
      ++handouts;
      printf("[cyc %llu] HANDOUT #%d: freeRegAddr=%u  free33=%u  map(14)=%u  pc=%llx\n",
             (unsigned long long)cyc, handouts, fra, f, m14,
             (unsigned long long)tb_->robOut0_pc);
    }
    // free-bit transitions bracket the ownership windows.
    if (f != prev_free)
      printf("[cyc %llu] free33: %u -> %u   map(14)=%u  pc=%llx\n",
             (unsigned long long)cyc, prev_free, f, m14,
             (unsigned long long)tb_->robOut0_pc);

    prev_free = f; prev_fra = fra;
  }
  printf("\ntotal p%u hand-outs in [%llu,%llu): %d\n", WATCH,
         (unsigned long long)FROM, (unsigned long long)STOP, handouts);
  return 0;
}
