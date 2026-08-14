// msip_arb_probe.cpp — pin down the mt-ipitmr interrupt livelock: when hart0's
// MSIP stays asserted for THRESHOLD cycles, log every subsequent cycle in which
// any core writes hart0's CLINT msip, tagging the source uart, and print the
// resulting msipShared_0. In mt-ipitmr only hart0 clears its own msip
// (uart0 -> hart0, data 0) and peers only set it (uartN -> hart0, data 1), so
// the source uart alone tells set vs clear. This shows whether hart0's clear
// ever lands or is perpetually overridden by peer sets (set-wins arbitration).
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define PERIPH(sig) tb_->system__DOT__peripherals__DOT__##sig
#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipitmr-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";
  auto env64 = [](const char *n, uint64_t d){ const char*v=getenv(n); return v?strtoull(v,0,0):d; };
  const uint64_t END = env64("END", 15000000ULL);
  const uint64_t THRESHOLD = env64("THRESHOLD", 250000ULL);
  const uint64_t TRACE = env64("TRACE", 6000ULL);

  simulator bench; bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0, set_since = 0; uint32_t prev = 0;
  auto wr_hart0 = [&](int u) -> bool {
    switch (u) {
      case 0: return PERIPH(uart0_msipWrite_valid) && PERIPH(uart0_msipWrite_hart) == 0;
      case 1: return PERIPH(uart1_msipWrite_valid) && PERIPH(uart1_msipWrite_hart) == 0;
      case 2: return PERIPH(uart2_msipWrite_valid) && PERIPH(uart2_msipWrite_hart) == 0;
      default:return PERIPH(uart3_msipWrite_valid) && PERIPH(uart3_msipWrite_hart) == 0;
    }
  };

  while (cyc < END) {
    tb_->eval(); tb_->clock = 1; tb_->eval(); tb_->clock = 0; tb_->eval(); ++cyc;
    uint32_t m0 = (uint32_t)PERIPH(msipShared_0);
    if (m0 == 1 && prev == 0) set_since = cyc;
    prev = m0;
    if (m0 == 1 && (cyc - set_since) > THRESHOLD) {
      printf("[WEDGE %llu] hart0 msip pinned %llu cyc; tracing msip[0] writes for %llu cyc\n",
             (unsigned long long)cyc, (unsigned long long)(cyc - set_since),
             (unsigned long long)TRACE);
      fflush(stdout);
      uint64_t clr=0, set=0, drops_to_0=0;
      for (uint64_t k=0;k<TRACE;++k){
        tb_->eval(); tb_->clock=1; tb_->eval(); tb_->clock=0; tb_->eval(); ++cyc;
        uint32_t before = (uint32_t)PERIPH(msipShared_0);
        bool w0=wr_hart0(0),w1=wr_hart0(1),w2=wr_hart0(2),w3=wr_hart0(3);
        uint32_t after = (uint32_t)PERIPH(msipShared_0);
        if (w0) clr++;               // uart0 -> hart0 : hart0's own clear
        if (w1||w2||w3) set++;       // peer -> hart0  : send_msip set
        if (before==1 && after==0) drops_to_0++;
        if (w0 || w1 || w2 || w3) {
          printf("  cyc=%llu wr{u0=%d u1=%d u2=%d u3=%d} msip0 %u->%u hart0pc=%llx canSoft0=%d\n",
                 (unsigned long long)cyc, w0,w1,w2,w3, before, after,
                 (unsigned long long)tb_->robOut0_pc,
                 (int)CORE_RAW(0, decode_canTakeSoftInterrupt));
          fflush(stdout);
        }
      }
      printf("[summary] over %llu cyc: hart0-clear-writes=%llu peer-set-writes=%llu msip0 1->0 transitions=%llu\n",
             (unsigned long long)TRACE,(unsigned long long)clr,(unsigned long long)set,
             (unsigned long long)drops_to_0);
      fflush(stdout);
      return 7;
    }
    if ((cyc % 5000000ULL)==0){ printf("[prog %llu] msip0=%u\n",(unsigned long long)cyc,m0); fflush(stdout);}
  }
  printf("done cyc=%llu no wedge\n",(unsigned long long)cyc);
  return 0;
}
