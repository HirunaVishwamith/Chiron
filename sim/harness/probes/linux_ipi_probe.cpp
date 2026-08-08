// linux_ipi_probe.cpp — why does Linux hang after "Run /init as init process"?
//
// STATE OF THE INVESTIGATION
// --------------------------
// Quad-core Linux boots, prints "Run /init as init process" at ~763M cycles,
// then goes silent. At 1.589 BILLION cycles (15.5 h of wall clock) hart1 is in
// smp_call_function_many_cond+0x2e4 — csd_lock_wait — while harts 0/2/3 cycle
// through do_idle / tick_nohz_idle_stop_tick / get_next_timer_interrupt.
// Nothing is wedged: every hart advances and the ROB is healthy, so this is NOT
// the reallocated-slot pipeline wedge that was fixed in 1c00664.
//
// Hart1 is waiting for a peer to service a cross-call. Two candidate faults
// have already been RULED OUT by fast bare-metal repros:
//   * mt-ipiwfi  PASS — IPIs are delivered even to a hart idling on WFI, so
//                       WFI-as-NOP suppressing interrupt injection is not it.
//   * mt-csdwait PASS — a spin-on-load loop containing cpu_relax() (which on
//                       RISC-V is literally `div x,x,zero`) does observe a
//                       peer's store, so this is not a stale-load/coherence bug.
// What remains is upstream of both: the target hart never runs the callback.
// That splits cleanly in two, and this probe is built to tell them apart:
//
//   SENDER side   — Linux never writes the target's CLINT msip at all (the
//                   cross-call is queued but no IPI is raised, e.g. the ipi_mux
//                   demux layer or the cpumask never reaches the CLINT).
//   RECEIVER side — msip IS written, the line goes high, but the target never
//                   traps (mstatus.MIE / mie.MSIE clear, mip.MSIP not latching,
//                   or the interrupt-inject FSM never finding a window).
//
// Counting msip writes per target hart answers that directly, which no amount
// of PC sampling can. Everything else printed here exists to interpret the
// answer: if msip writes ARE arriving, the per-hart mstatus/mie/mip trio says
// which gate is shut; if they are NOT, the fault is in the kernel's send path
// and the RTL's IPI hardware is exonerated.
//
// The heartbeat also reports cycles-since-last-UART-byte, because the console
// going quiet is the symptom the whole hunt started from.
//
// Usage (same arguments as linux_sim.out):
//   build/linux_ipi_probe.out bins/linux-q4.bin sim/data/qemu.dtb sim/data/boot.bin
// Env:
//   IPI_REPORT_CYCLES  cycles between diagnostic lines (default 20,000,000)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>

#include "sim/rtl/rtl_model.h"

#define CORE(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__##s

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  const char *rp = getenv("IPI_REPORT_CYCLES");
  const uint64_t REPORT = rp ? strtoull(rp, nullptr, 0) : 20000000ULL;

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb = bench.raw();

  std::printf("\n***** Linux IPI probe: booting quad-core image *****\n");
  std::printf("      counting CLINT msip writes per target hart; the console\n");
  std::printf("      going quiet is the symptom, msip counts are the evidence\n\n");
  std::fflush(stdout);

  uint64_t cyc = 0;
  uint64_t msip_writes[4] = {0, 0, 0, 0};
  uint64_t last_msip_writes[4] = {0, 0, 0, 0};
  uint64_t last_uart_cyc = 0;
  uint64_t uart_bytes = 0;
  uint64_t next_report = REPORT;

  for (;;) {
    tb->eval();

    // CLINT msip writes are single-cycle pulses, which is why this probe has
    // its own per-cycle loop instead of extending linux_sim.cpp (whose loop
    // advances one COMMIT at a time and would miss them). Each core's port has
    // its own write channel into the shared msip block.
#define COUNT_MSIP(N)                                                          \
    if (tb->system__DOT__peripherals__DOT__uart##N##_msipWrite_valid) {         \
      unsigned h = (unsigned)tb->system__DOT__peripherals__DOT__uart##N##_msipWrite_hart; \
      if (h < 4) msip_writes[h]++;                                             \
    }
    COUNT_MSIP(0) COUNT_MSIP(1) COUNT_MSIP(2) COUNT_MSIP(3)
#undef COUNT_MSIP

    // Console. Any hart's uartPort can drive it; core0 is the usual one.
#define UART(N)                                                                \
    if (tb->core##N##OutChar_valid) {                                          \
      putchar((int)tb->core##N##OutChar_byte);                                 \
      fflush(stdout);                                                          \
      last_uart_cyc = cyc; uart_bytes++;                                       \
    }
    UART(0) UART(1) UART(2) UART(3)
#undef UART

    if (cyc >= next_report) {
      next_report = cyc + REPORT;
      const uint64_t pc[4] = {
          (uint64_t)tb->robOut0_pc, (uint64_t)tb->robOut1_pc,
          (uint64_t)tb->robOut2_pc, (uint64_t)tb->robOut3_pc};
      const unsigned msip_level[4] = {
          (unsigned)tb->system__DOT__peripherals__DOT__msipShared_0,
          (unsigned)tb->system__DOT__peripherals__DOT__msipShared_1,
          (unsigned)tb->system__DOT__peripherals__DOT__msipShared_2,
          (unsigned)tb->system__DOT__peripherals__DOT__msipShared_3};
      uint64_t mstatus[4], mie[4], mip[4], mcause[4];
#define CSRS(N)                                                                \
      mstatus[N] = (uint64_t)CORE(N, decode__DOT__mstatus);                     \
      mie[N]     = (uint64_t)CORE(N, decode__DOT__mie);                         \
      mip[N]     = (uint64_t)CORE(N, decode__DOT__mip);                         \
      mcause[N]  = (uint64_t)CORE(N, decode__DOT__mcause);
      CSRS(0) CSRS(1) CSRS(2) CSRS(3)
#undef CSRS

      std::fprintf(stderr,
          "\n[ipi] cyc=%" PRIu64 "  uart_bytes=%" PRIu64
          "  quiet_for=%" PRIu64 " cycles\n", cyc, uart_bytes,
          cyc - last_uart_cyc);
      for (int h = 0; h < 4; h++) {
        // mstatus.MIE is bit 3; mie/mip.MSIE/MSIP is bit 3, MTIE/MTIP is bit 7.
        std::fprintf(stderr,
            "[ipi]   hart%d pc=%08" PRIx64 "  msip_writes=%" PRIu64
            " (+%" PRIu64 ")  msip_line=%u  MIE=%u MSIE=%u MSIP=%u"
            "  MTIE=%u MTIP=%u  mcause=%" PRIx64 "\n",
            h, pc[h], msip_writes[h], msip_writes[h] - last_msip_writes[h],
            msip_level[h],
            (unsigned)((mstatus[h] >> 3) & 1), (unsigned)((mie[h] >> 3) & 1),
            (unsigned)((mip[h] >> 3) & 1), (unsigned)((mie[h] >> 7) & 1),
            (unsigned)((mip[h] >> 7) & 1), mcause[h]);
        last_msip_writes[h] = msip_writes[h];
      }
      // The reading, spelled out so a log is self-explanatory months later.
      std::fprintf(stderr,
          "[ipi]   => if a hart is spinning in csd_lock_wait and its PEERS show"
          " msip_writes NOT advancing,\n"
          "[ipi]      the kernel never raised the IPI (sender side). If they DO"
          " advance but msip_line stays\n"
          "[ipi]      high with MSIP=1 and the hart does not trap, the receiver"
          " gate is shut (MIE/MSIE).\n");
      std::fflush(stderr);
    }

    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    ++cyc;
  }
  return 0;
}
