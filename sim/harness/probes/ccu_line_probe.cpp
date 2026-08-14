// ccu_line_probe.cpp — boot a Linux image RTL-only and log every CCU FSM_12
// transaction + data beat that touches a set of watched physical lines, with
// the CCU pipeline state (crpbuf snoop summary, selected data source) and
// each D-cache's in-service snoop address. Built to catch a CD-channel /
// transaction data crossing red-handed (kernel-text bytes landing in the
// CPU2 spinlock line 0x806b2c00 — source line 0x80274c40).
//
// Build:  make build/ccu_line_probe.out
// Run  :  build/ccu_line_probe.out bins/linux-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "sim/rtl/rtl_model.h"

#define CCU(sig) tb_->system__DOT__chiron__DOT__interconnect___DOT__CCU__DOT__##sig
#define CORE_SNOOP(n) \
  tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__aceUnit__DOT__coherencyRequestBuffer_address

// Watched 64B lines. Defaults preserve the original spinlock-corruption pair;
// override with env CCU_WATCH="addr1,addr2,..." (hex, line-aligned or not).
static uint64_t watch_base[8];
static int n_watch = 0;
static void init_watch(void) {
  const char *env = getenv("CCU_WATCH");
  if (!env || !*env) {
    watch_base[n_watch++] = 0x806b2c00ULL;
    watch_base[n_watch++] = 0x80274c40ULL;
    return;
  }
  char buf[256]; snprintf(buf, sizeof buf, "%s", env);
  for (char *tok = strtok(buf, ","); tok && n_watch < 8; tok = strtok(nullptr, ","))
    watch_base[n_watch++] = strtoull(tok, nullptr, 16) & ~0x3fULL;
}
static bool watched(uint64_t a) {
  for (int i = 0; i < n_watch; ++i)
    if (a >= watch_base[i] && a < watch_base[i] + 0x40ULL) return true;
  return false;
}

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  init_watch();
  for (int i = 0; i < n_watch; ++i)
    printf("[ccu] watching line 0x%llx\n", (unsigned long long)watch_base[i]);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0;
  int prev_s12 = 0;
  bool in_watched_txn = false;
  int beat_n = 0;
  bool print_beat_next = false;

  // Raw per-cycle ticking: FSM_12 states last one cycle each, so sampling at
  // commit granularity would miss them.
  while (cyc < 200000000ULL) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if ((cyc % 10000000ULL) == 0) {
      printf("[ccu] progress cyc=%llu pc3=0x%llx\n", (unsigned long long)cyc,
             (unsigned long long)tb_->robOut3_pc);
      fflush(stdout);
    }

    int s12 = CCU(stateReg_12);
    uint64_t addr3 = CCU(addr_pbuf_3);

    if (watched(addr3) && !in_watched_txn && s12 != 0) {
      in_watched_txn = true;
      beat_n = 0;
      printf("[ccu] cyc=%llu TXN addr=0x%llx tran=0x%x reqcore=%d "
             "crp={%02x,%02x,%02x,%02x} sel=%d snoops={%llx,%llx,%llx,%llx}\n",
             (unsigned long long)cyc, (unsigned long long)addr3,
             (unsigned)CCU(tran_pbuf_3), (int)CCU(core_id_pbuf_3),
             (unsigned)CCU(crpbuf_3_0), (unsigned)CCU(crpbuf_3_2),
             (unsigned)CCU(crpbuf_3_4), (unsigned)CCU(crpbuf_3_6),
             (int)CCU(select_buff),
             (unsigned long long)CORE_SNOOP(0), (unsigned long long)CORE_SNOOP(1),
             (unsigned long long)CORE_SNOOP(2), (unsigned long long)CORE_SNOOP(3));
      fflush(stdout);
    }
    if (in_watched_txn) {
      if (print_beat_next) {
        print_beat_next = false;
        printf("[ccu] cyc=%llu   beat%d=0x%016llx last=%d sel=%d\n",
               (unsigned long long)cyc, beat_n,
               (unsigned long long)CCU(beat_buff), (int)CCU(last_buff),
               (int)CCU(select_buff));
        fflush(stdout);
        beat_n++;
      }
      if (s12 == 3 && prev_s12 != 3) print_beat_next = true;
      if (s12 == 0 && prev_s12 != 0) {
        printf("[ccu] cyc=%llu TXN END addr=0x%llx beats_seen=%d\n",
               (unsigned long long)cyc, (unsigned long long)addr3, beat_n);
        in_watched_txn = false;
        fflush(stdout);
      }
    }
    prev_s12 = s12;
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
