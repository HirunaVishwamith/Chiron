// div_park_probe.cpp — boot a Linux image RTL-only; when any hart's ROB-head
// PC parks >1M cycles, dump that core's M-unit internals, and for core 3 also
// the full 8-entry scheduler queue (valid / isM / rs-ready bits / robAddr),
// distinguishing a lost scheduler wakeup from a lost M-unit release.
//
// Build:  make build/div_park_probe.out
// Run  :  build/div_park_probe.out bins/linux-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include "sim/rtl/rtl_model.h"

#define CASE_CORE(n, sig) case n: return tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig
#define Q3(e, f) tb_->system__DOT__chiron__DOT__core3__DOT__scheduler__DOT__queue_##e##_##f

#define DUMP_Q3_ENTRY(tb_, e)                                                  \
  printf("[probe]   q%d v=%d insn=%08x isM=%d rob=%x rs1(r=%d p=%02x) "        \
         "rs2(r=%d p=%02x) dest=%02x bmask=%02x\n",                            \
         e, (int)Q3(e, valid), (unsigned)Q3(e, instruction),                   \
         (int)Q3(e, opcodeMeta_isM), (unsigned)Q3(e, robAddr),                 \
         (int)Q3(e, rs1_ready), (unsigned)Q3(e, rs1_prfAddr),                  \
         (int)Q3(e, rs2_ready), (unsigned)Q3(e, rs2_prfAddr),                  \
         (unsigned)Q3(e, prfDest), (unsigned)Q3(e, branchMask))

struct MProbe {
  Vsystem *tb_;
  explicit MProbe(Vsystem *t) : tb_(t) {}
  uint64_t ready(int h) {
    switch (h) { CASE_CORE(0, mExtensionReady); CASE_CORE(1, mExtensionReady);
                 CASE_CORE(2, mExtensionReady); default: CASE_CORE(3, mExtensionReady); }
  }
  uint64_t div_v(int h) {
    switch (h) { CASE_CORE(0, division_request_valid); CASE_CORE(1, division_request_valid);
                 CASE_CORE(2, division_request_valid); default: CASE_CORE(3, division_request_valid); }
  }
  uint64_t div_cnt(int h) {
    switch (h) { CASE_CORE(0, division_counter); CASE_CORE(1, division_counter);
                 CASE_CORE(2, division_counter); default: CASE_CORE(3, division_counter); }
  }
  uint64_t req_v(int h) {
    switch (h) { CASE_CORE(0, extnMRequest_valid); CASE_CORE(1, extnMRequest_valid);
                 CASE_CORE(2, extnMRequest_valid); default: CASE_CORE(3, extnMRequest_valid); }
  }
  uint64_t req_insn(int h) {
    switch (h) { CASE_CORE(0, extnMRequest_instruction); CASE_CORE(1, extnMRequest_instruction);
                 CASE_CORE(2, extnMRequest_instruction); default: CASE_CORE(3, extnMRequest_instruction); }
  }
  uint64_t done(int h) {
    switch (h) { CASE_CORE(0, divDone); CASE_CORE(1, divDone);
                 CASE_CORE(2, divDone); default: CASE_CORE(3, divDone); }
  }
};

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  MProbe mp(bench.raw());

  uint64_t last_pc[4] = {}, still[4] = {};
  uint64_t cyc = 0;
  uint64_t next_report = 10000000;

  while (cyc < 800000000ULL) {
    uint8_t mask = 0;
    int rc = bench.step_until_commits_nodump(&mask);
    cyc = bench.tickcount + bench.dump_tick;
    if (rc == 1) {
      printf("GLOBAL STALL at cycle %llu\n", (unsigned long long)cyc);
      break;
    }
    for (int h = 0; h < 4; ++h) {
      uint64_t pc = bench.core_pc(h);
      if (pc != last_pc[h]) { last_pc[h] = pc; still[h] = cyc; }
    }
    if (cyc >= next_report) {
      next_report += 10000000;
      printf("[probe] cyc=%llu pcs=%llx/%llx/%llx/%llx\n",
             (unsigned long long)cyc,
             (unsigned long long)bench.core_pc(0), (unsigned long long)bench.core_pc(1),
             (unsigned long long)bench.core_pc(2), (unsigned long long)bench.core_pc(3));
      for (int h = 0; h < 4; ++h) {
        if (cyc - still[h] > 1000000) {  // parked >1M cycles
          printf("[probe] h%d PARKED at 0x%llx for %lluk cyc: "
                 "ready=%llu div_v=%llu cnt=%llu req_v=%llu req_insn=%08llx done=%llu\n",
                 h, (unsigned long long)last_pc[h],
                 (unsigned long long)((cyc - still[h]) / 1000),
                 (unsigned long long)mp.ready(h), (unsigned long long)mp.div_v(h),
                 (unsigned long long)mp.div_cnt(h), (unsigned long long)mp.req_v(h),
                 (unsigned long long)mp.req_insn(h), (unsigned long long)mp.done(h));
          if (h == 3) {
            Vsystem *tb_ = bench.raw();
            DUMP_Q3_ENTRY(tb_, 0); DUMP_Q3_ENTRY(tb_, 1);
            DUMP_Q3_ENTRY(tb_, 2); DUMP_Q3_ENTRY(tb_, 3);
            DUMP_Q3_ENTRY(tb_, 4); DUMP_Q3_ENTRY(tb_, 5);
            DUMP_Q3_ENTRY(tb_, 6); DUMP_Q3_ENTRY(tb_, 7);
          }
        }
      }
      fflush(stdout);
    }
  }
  return 0;
}
