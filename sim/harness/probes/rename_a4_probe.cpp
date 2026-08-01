// rename_a4_probe.cpp — determine HOW a4 is corrupted in mt-ipitmr.
//
// registersOut[n] is not a plain architectural register file. From
// chironCore.scala:45 it is
//     architecturalRegisterFile = retiredRenamedTable.table.map(prf.registerFileOutput(_))
// i.e. registersOut[14] == PRF[ architecturalRegMap(14) ]. So the observed
// "a4 changes from 3 to 0x290 with commitFired=0" (rob_integrity_probe, cyc
// 9067755) has exactly two possible causes:
//
//   (a) architecturalRegMap(14) CHANGED — a bogus rename update pointed the
//       committed mapping of a4 at a different physical register; or
//   (b) the mapping stayed put and the PRF entry it names was OVERWRITTEN by
//       another instruction — i.e. that physical register was freed/reallocated
//       while still being a4's committed mapping (premature release).
//
// Those need opposite fixes, and the register value alone cannot tell them
// apart. This probe logs architecturalRegMap(14) alongside registersOut0_14 so
// the two are directly comparable at the corruption cycle.
//
// Build: make build/rename_a4_probe.out
// Run  : build/rename_a4_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig) tb_->system__DOT__chiron__DOT__core0__DOT__##sig

struct Ev {
  uint64_t cyc, pc, a4;
  unsigned map14, fired, wbFired, wbRd, wbPrf;
  uint64_t wbInsn;
  unsigned beV, beP, inj;
  unsigned free33, rsv33;
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t STOP = env64("STOP", 9067760ULL);  // just past the corruption
  const int      KEEP = (int)env64("KEEP", 120);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  static const int RING = 4096;
  static Ev ring[RING];
  int head = 0; uint64_t nev = 0;
  uint64_t cyc = 0, prev_a4 = ~0ULL; unsigned prev_map = ~0u, prev_free = ~0u;

  while (cyc < STOP) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const uint64_t a4 = tb_->registersOut0_14;
    const unsigned m14 = (unsigned)C0(decode__DOT__architecturalRegMap_14);
    const unsigned f33 = (unsigned)C0(decode__DOT__PRFFreeList_33);
    if (a4 != prev_a4 || m14 != prev_map || f33 != prev_free) {
      Ev &e = ring[head];
      e.cyc = cyc; e.pc = tb_->robOut0_pc; e.a4 = a4; e.map14 = m14;
      e.fired = (unsigned)tb_->robOut0_commitFired;
      // Only `fired` survives Verilator's optimiser here; rdAddr/PRFDest/
      // instruction are inlined away, so they are not sampled.
      e.wbFired = (unsigned)C0(decode_writeBackResult_fired);
      e.wbRd = 0; e.wbPrf = 0; e.wbInsn = 0;
      e.beV = (unsigned)C0(branchEvals_valid);
      e.beP = (unsigned)C0(branchEvals_passed);
      e.inj = (unsigned)C0(interruptInjectStatus);
      // p33 is a4's committed mapping across the corruption. If its free-list
      // bit goes TRUE while it is still architecturally live, the overwrite is
      // a premature release + reallocation; if it stays FALSE, something else
      // is writing PRF[33] and the free list is exonerated.
      e.free33 = (unsigned)C0(decode__DOT__PRFFreeList_33);
      e.rsv33  = (unsigned)C0(decode__DOT__reservedFreeList1_33);
      head = (head + 1) % RING; ++nev;
    }
    prev_a4 = a4; prev_map = m14; prev_free = f33;
  }

  printf("a4 / rename-map history up to cyc=%llu\n", (unsigned long long)STOP);
  printf("%-11s %-10s %-10s %-7s %-5s %-6s %-5s %-6s %-10s %s\n",
         "cyc","headPC","a4","map(14)","fire","wbFire","free33","rsv33","-","beV/beP/inj");
  int n = (int)(nev < (uint64_t)KEEP ? nev : (uint64_t)KEEP);
  for (int i = n; i > 0; --i) {
    const Ev &e = ring[(head - i + RING) % RING];
    printf("%-11llu %-10llx %-10llx %-7u %-5u %-6u %-5u %-6u %-10llx %u/%u/%u\n",
           (unsigned long long)e.cyc, (unsigned long long)e.pc,
           (unsigned long long)e.a4, e.map14, e.fired, e.wbFired,
           e.free33, e.rsv33, (unsigned long long)e.wbInsn, e.beV, e.beP, e.inj);
  }
  return 0;
}
