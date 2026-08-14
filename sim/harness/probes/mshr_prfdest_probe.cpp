// mshr_prfdest_probe.cpp — find who writes PRF[33] and clobbers a4 in mt-ipitmr.
//
// Chain of evidence so far:
//   * a4 == PRF[architecturalRegMap(14)] (chironCore.scala:45).
//   * At cyc 9067755 a4 goes 3 -> 0x290 with commitFired=0 and map(14) UNCHANGED
//     at 33, so it is not a rename update.
//   * PRFFreeList_33 and reservedFreeList1_33 are both 0 (allocated / reserved)
//     across the whole window, so p33 was never released — not a free-list bug.
// => something wrote PRF[33] while p33 was legitimately a4's committed mapping.
//    The natural carrier is a long-latency memory request that outlived the
//    instruction it belonged to: the ACE MSHR holds core_prfDest per entry, so a
//    load squashed by a mispredict whose MSHR entry is not cancelled will refill
//    later and write a physical register that has since been reallocated.
//
// The MSHR entries do carry branch_mask/branch_valid, i.e. squash support exists;
// this probe checks whether it is actually applied. It dumps every entry holding
// core_prfDest == WATCH (default 33) as it appears/changes, and dumps the full
// MSHR around the corruption cycle.
//
// Build: make build/mshr_prfdest_probe.out
// Run  : build/mshr_prfdest_probe.out bins/mt-ipitmr-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define C0(sig)  tb_->system__DOT__chiron__DOT__core0__DOT__##sig
#define MSHR(i, f) C0(memAccess__DOT__aceUnit__DOT__ACEMSHR__DOT__memReg_##i##_##f)

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t STOP  = env64("STOP",  9067760ULL);
  const uint64_t FROM  = env64("FROM",  9067300ULL);   // start verbose window
  const unsigned WATCH = (unsigned)env64("WATCH", 33ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  unsigned prev_v[16] = {0}, prev_d[16] = {0};

  auto ent_valid = [&](int i) -> unsigned {
    switch (i) {
      case 0: return MSHR(0,valid);   case 1: return MSHR(1,valid);
      case 2: return MSHR(2,valid);   case 3: return MSHR(3,valid);
      case 4: return MSHR(4,valid);   case 5: return MSHR(5,valid);
      case 6: return MSHR(6,valid);   case 7: return MSHR(7,valid);
      case 8: return MSHR(8,valid);   case 9: return MSHR(9,valid);
      case 10: return MSHR(10,valid); case 11: return MSHR(11,valid);
      case 12: return MSHR(12,valid); case 13: return MSHR(13,valid);
      case 14: return MSHR(14,valid); default: return MSHR(15,valid);
    }
  };
  auto ent_dest = [&](int i) -> unsigned {
    switch (i) {
      case 0: return MSHR(0,core_prfDest);   case 1: return MSHR(1,core_prfDest);
      case 2: return MSHR(2,core_prfDest);   case 3: return MSHR(3,core_prfDest);
      case 4: return MSHR(4,core_prfDest);   case 5: return MSHR(5,core_prfDest);
      case 6: return MSHR(6,core_prfDest);   case 7: return MSHR(7,core_prfDest);
      case 8: return MSHR(8,core_prfDest);   case 9: return MSHR(9,core_prfDest);
      case 10: return MSHR(10,core_prfDest); case 11: return MSHR(11,core_prfDest);
      case 12: return MSHR(12,core_prfDest); case 13: return MSHR(13,core_prfDest);
      case 14: return MSHR(14,core_prfDest); default: return MSHR(15,core_prfDest);
    }
  };
  auto ent_bmask = [&](int i) -> unsigned {
    switch (i) {
      case 0: return MSHR(0,branch_mask);   case 1: return MSHR(1,branch_mask);
      case 2: return MSHR(2,branch_mask);   case 3: return MSHR(3,branch_mask);
      case 4: return MSHR(4,branch_mask);   case 5: return MSHR(5,branch_mask);
      case 6: return MSHR(6,branch_mask);   case 7: return MSHR(7,branch_mask);
      case 8: return MSHR(8,branch_mask);   case 9: return MSHR(9,branch_mask);
      case 10: return MSHR(10,branch_mask); case 11: return MSHR(11,branch_mask);
      case 12: return MSHR(12,branch_mask); case 13: return MSHR(13,branch_mask);
      case 14: return MSHR(14,branch_mask); default: return MSHR(15,branch_mask);
    }
  };
  auto ent_bvalid = [&](int i) -> unsigned {
    switch (i) {
      case 0: return MSHR(0,branch_valid);   case 1: return MSHR(1,branch_valid);
      case 2: return MSHR(2,branch_valid);   case 3: return MSHR(3,branch_valid);
      case 4: return MSHR(4,branch_valid);   case 5: return MSHR(5,branch_valid);
      case 6: return MSHR(6,branch_valid);   case 7: return MSHR(7,branch_valid);
      case 8: return MSHR(8,branch_valid);   case 9: return MSHR(9,branch_valid);
      case 10: return MSHR(10,branch_valid); case 11: return MSHR(11,branch_valid);
      case 12: return MSHR(12,branch_valid); case 13: return MSHR(13,branch_valid);
      case 14: return MSHR(14,branch_valid); default: return MSHR(15,branch_valid);
    }
  };
  auto ent_rob = [&](int i) -> unsigned {
    switch (i) {
      case 0: return MSHR(0,core_robAddr);   case 1: return MSHR(1,core_robAddr);
      case 2: return MSHR(2,core_robAddr);   case 3: return MSHR(3,core_robAddr);
      case 4: return MSHR(4,core_robAddr);   case 5: return MSHR(5,core_robAddr);
      case 6: return MSHR(6,core_robAddr);   case 7: return MSHR(7,core_robAddr);
      case 8: return MSHR(8,core_robAddr);   case 9: return MSHR(9,core_robAddr);
      case 10: return MSHR(10,core_robAddr); case 11: return MSHR(11,core_robAddr);
      case 12: return MSHR(12,core_robAddr); case 13: return MSHR(13,core_robAddr);
      case 14: return MSHR(14,core_robAddr); default: return MSHR(15,core_robAddr);
    }
  };
  auto ent_addr = [&](int i) -> uint64_t {
    switch (i) {
      case 0: return MSHR(0,address);   case 1: return MSHR(1,address);
      case 2: return MSHR(2,address);   case 3: return MSHR(3,address);
      case 4: return MSHR(4,address);   case 5: return MSHR(5,address);
      case 6: return MSHR(6,address);   case 7: return MSHR(7,address);
      case 8: return MSHR(8,address);   case 9: return MSHR(9,address);
      case 10: return MSHR(10,address); case 11: return MSHR(11,address);
      case 12: return MSHR(12,address); case 13: return MSHR(13,address);
      case 14: return MSHR(14,address); default: return MSHR(15,address);
    }
  };

  printf("watching ACE MSHR for core_prfDest == %u (a4's physical reg)\n\n", WATCH);
  while (cyc < STOP) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    for (int i = 0; i < 16; ++i) {
      const unsigned v = ent_valid(i), d = ent_dest(i);
      const bool hit = (d == WATCH) && v;
      const bool changed = (v != prev_v[i]) || (d != prev_d[i]);
      if (hit && changed) {
        printf("[cyc %llu] MSHR[%2d] valid=%u prfDest=%u robAddr=%u "
               "brMask=%x brValid=%u addr=%llx  <== holds a4's phys reg\n",
               (unsigned long long)cyc, i, v, d, ent_rob(i),
               ent_bmask(i), ent_bvalid(i), (unsigned long long)ent_addr(i));
      }
      prev_v[i] = v; prev_d[i] = d;
    }

    if (cyc >= FROM && (cyc % 50) == 0) {
      printf("[cyc %llu] mshr:", (unsigned long long)cyc);
      for (int i = 0; i < 16; ++i)
        if (ent_valid(i))
          printf(" [%d]d=%u,rob=%u,bm=%x,bv=%u", i, ent_dest(i), ent_rob(i),
                 ent_bmask(i), ent_bvalid(i));
      printf("\n");
    }
  }
  return 0;
}
