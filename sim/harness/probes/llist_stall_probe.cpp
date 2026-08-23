// llist_stall_probe.cpp — where does mt-llist actually stop?
//
// mt-llist hangs with no UART output, which is indistinguishable between "the
// RTL wedged", "a hart is livelocked in an LR/SC retry", and "the test itself
// is spinning in a loop with no exit". This samples each hart's committed PC
// and the LR/SC reservation machinery, then reports a per-hart PC histogram so
// the stall site names itself.
//
// Build:  make build/llist_stall_probe.out
// Run  :  build/llist_stall_probe.out bins/mt-llist-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <map>
#include "sim/rtl/rtl_model.h"

#define CL(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-llist-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("LLP_END",    6000000ULL);
  const uint64_t SAMPLE    = env64("LLP_SAMPLE",     997ULL);  // prime: avoid aliasing a loop period
  const uint64_t HIST_FROM = env64("LLP_FROM",   2000000ULL);  // after the run should have finished

  std::map<uint64_t, uint64_t> hist[4];
  // Progress counters: a commit landing on the instruction *after* a
  // successful CAS means one push retired. Counting these answers the only
  // question that matters -- is this wedged, or merely slow?
  const uint64_t PC_PUSH_OK = env64("LLP_PUSHPC", 0x80000560ULL);
  const uint64_t PC_DRAIN   = env64("LLP_DRAINPC", 0x800005e0ULL);
  uint64_t pushes[4] = {0,0,0,0}, drains = 0;
  uint64_t prev_pc[4] = {0,0,0,0};
  uint64_t next_report = env64("LLP_REPORT", 1000000ULL);
  const uint64_t REPORT_EVERY = next_report;
  uint64_t sc_fail_seen[4] = {0, 0, 0, 0};
  uint64_t last_resv[4]    = {0, 0, 0, 0};

  uint64_t cyc = 0;
  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    // Count reservation drops: a reserved->not-reserved transition is a peer
    // write killing this hart's reservation, which is what starves an sc.d.
    const uint64_t r[4] = {CL(0, reservationRegister_reserved),
                           CL(1, reservationRegister_reserved),
                           CL(2, reservationRegister_reserved),
                           CL(3, reservationRegister_reserved)};
    for (int i = 0; i < 4; i++) {
      if (last_resv[i] && !r[i]) sc_fail_seen[i]++;
      last_resv[i] = r[i];
    }

    {
      const uint64_t p0[4] = {tb_->robOut0_pc, tb_->robOut1_pc,
                              tb_->robOut2_pc, tb_->robOut3_pc};
      for (int i = 0; i < 4; i++) {
        if (p0[i] != prev_pc[i]) {
          if (p0[i] == PC_PUSH_OK) pushes[i]++;
          if (i == 0 && p0[i] == PC_DRAIN) drains++;
          prev_pc[i] = p0[i];
        }
      }
    }
    if (cyc >= next_report) {
      next_report += REPORT_EVERY;
      printf("[%8llu] pushes c1=%llu c2=%llu c3=%llu (total=%llu) drains=%llu\n",
             (unsigned long long)cyc, (unsigned long long)pushes[1],
             (unsigned long long)pushes[2], (unsigned long long)pushes[3],
             (unsigned long long)(pushes[1]+pushes[2]+pushes[3]),
             (unsigned long long)drains);
      fflush(stdout);
    }

    if (cyc % SAMPLE) continue;
    if (cyc < HIST_FROM) continue;
    const uint64_t pc[4] = {tb_->robOut0_pc, tb_->robOut1_pc,
                            tb_->robOut2_pc, tb_->robOut3_pc};
    for (int i = 0; i < 4; i++) hist[i][pc[i]]++;
  }

  printf("=== mt-llist stall profile (cycles %llu..%llu) ===\n",
         (unsigned long long)HIST_FROM, (unsigned long long)END);
  for (int i = 0; i < 4; i++) {
    printf("core%d  reservation-drops=%llu\n", i,
           (unsigned long long)sc_fail_seen[i]);
    // Top 5 PCs this hart sat at.
    std::multimap<uint64_t, uint64_t, std::greater<uint64_t>> top;
    uint64_t total = 0;
    for (auto &kv : hist[i]) { top.insert({kv.second, kv.first}); total += kv.second; }
    int shown = 0;
    for (auto &kv : top) {
      if (shown++ >= 5) break;
      printf("    pc=0x%08llx  %5.1f%%  (%llu samples)\n",
             (unsigned long long)kv.second,
             total ? 100.0 * kv.first / total : 0.0,
             (unsigned long long)kv.first);
    }
    if (!total) printf("    (no samples)\n");
  }
  return 0;
}
