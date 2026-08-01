// ipi_hang_probe.cpp — boot linux-q4 RTL-only and trace the complete CLINT
// MSIP IPI timeline: every msipShared set/clear (who was targeted, all four
// pcs, each core's injection-FSM/mstatus/mie state), every machine-software
// trap actually taken (decode.mcause edge to 0x8000...0003), plus periodic
// snapshots in the hang window with DRAM peeks of the kernel's ipi_data[]
// pending-bits and stats. Built for the post-/init csd_lock_wait hang: CPU0
// parks at smp_call_function_many_cond+0x2e8 (pc 0x802952cc) while CPUs 1-3
// idle — this log shows whether the msip write ever happened, whether the
// target took the trap, and what its enable state was in between.
//
// Build:  make build/ipi_hang_probe.out
// Run  :  build/ipi_hang_probe.out bins/linux-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define PERIPH(sig) tb_->system__DOT__peripherals__DOT__##sig
#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

// Kernel 6.3-rc4 q4 System.map: ipi_data[NR_CPUS], entry = stats[6] u64
// (cacheline 64B) + bits (own cacheline) -> 128 B stride, bits at +64.
static const uint64_t IPI_DATA_VA = 0x806b0000ULL;
static const uint64_t DRAM_BASE   = 0x80000000ULL;

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  // Window is env-overridable so a longer follow-up run needs no rebuild.
  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("IPIPROBE_END",       870000000ULL);
  const uint64_t SNAP_FROM = env64("IPIPROBE_SNAP_FROM", 760000000ULL);
  const uint64_t SNAP_STEP = env64("IPIPROBE_SNAP_STEP",  10000000ULL);

  uint32_t msip_prev[4] = {0, 0, 0, 0};
  uint64_t mcause_prev[4] = {0, 0, 0, 0};
  bool tx_prev[4] = {false, false, false, false};
  char linebuf[512]; int linelen = 0;

  uint64_t cyc = 0, next_snap = SNAP_FROM;

  auto dump_cores = [&]() {
    printf("        msip={%u,%u,%u,%u}\n",
           (unsigned)PERIPH(msipShared_0), (unsigned)PERIPH(msipShared_1),
           (unsigned)PERIPH(msipShared_2), (unsigned)PERIPH(msipShared_3));
#define ONE(n)                                                                \
    printf("        core%d pc=%llx injSt=%d injSoft=%d canIrq=%d canSoft=%d " \
           "brCnt=%d mExtRdy=%d extnMReq=%d atomBusy=%d "                     \
           "mstatus=%llx mie=%llx mcause=%llx mepc=%llx\n", n,                \
           (unsigned long long)tb_->robOut##n##_pc,                           \
           (int)CORE_RAW(n, interruptInjectStatus),                           \
           (int)CORE_RAW(n, injectingSoftwareInterrupt),                      \
           (int)CORE_RAW(n, decode_canTakeInterrupt),                         \
           (int)CORE_RAW(n, decode_canTakeSoftInterrupt),                     \
           (int)CORE_RAW(n, branchCounter),                                   \
           (int)CORE_RAW(n, mExtensionReady),                                 \
           (int)CORE_RAW(n, extnMRequest_valid),                              \
           (int)CORE_RAW(n, memAccess__DOT__arbiter__DOT__atomicBusyState),   \
           (unsigned long long)CORE_RAW(n, decode__DOT__mstatus),             \
           (unsigned long long)CORE_RAW(n, decode__DOT__mie),                 \
           (unsigned long long)CORE_RAW(n, decode__DOT__mcause),              \
           (unsigned long long)CORE_RAW(n, decode__DOT__mepc))
    ONE(0); ONE(1); ONE(2); ONE(3);
#undef ONE
  };

  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    // ── UART console capture (edge-detected, any port), cycle-stamped lines ──
    const bool tx_v[4] = {tb_->core0OutChar_valid != 0, tb_->core1OutChar_valid != 0,
                          tb_->core2OutChar_valid != 0, tb_->core3OutChar_valid != 0};
    const char tx_b[4] = {(char)tb_->core0OutChar_byte, (char)tb_->core1OutChar_byte,
                          (char)tb_->core2OutChar_byte, (char)tb_->core3OutChar_byte};
    for (int p = 0; p < 4; ++p) {
      if (tx_v[p] && !tx_prev[p]) {
        char c = tx_b[p];
        if (c == '\n' || linelen >= (int)sizeof(linebuf) - 2) {
          linebuf[linelen] = 0;
          printf("[con %9llu] %s\n", (unsigned long long)cyc, linebuf);
          fflush(stdout);
          linelen = 0;
        } else if (c != '\r') {
          linebuf[linelen++] = c;
        }
      }
      tx_prev[p] = tx_v[p];
    }

    // ── msipShared edges: the raw IPI wire timeline ──
    const uint32_t msip_cur[4] = {
        (uint32_t)PERIPH(msipShared_0), (uint32_t)PERIPH(msipShared_1),
        (uint32_t)PERIPH(msipShared_2), (uint32_t)PERIPH(msipShared_3)};
    for (int h = 0; h < 4; ++h) {
      if (msip_cur[h] != msip_prev[h]) {
        printf("[msip %9llu] hart%d %u->%u pcs=%llx/%llx/%llx/%llx\n",
               (unsigned long long)cyc, h, (unsigned)msip_prev[h],
               (unsigned)msip_cur[h],
               (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
               (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
        dump_cores();
        fflush(stdout);
      }
      msip_prev[h] = msip_cur[h];
    }

    // ── machine-software-interrupt deliveries (mcause becomes 0x8..3) ──
    const uint64_t mc[4] = {
        (uint64_t)CORE_RAW(0, decode__DOT__mcause), (uint64_t)CORE_RAW(1, decode__DOT__mcause),
        (uint64_t)CORE_RAW(2, decode__DOT__mcause), (uint64_t)CORE_RAW(3, decode__DOT__mcause)};
    for (int h = 0; h < 4; ++h) {
      if (mc[h] != mcause_prev[h] && mc[h] == 0x8000000000000003ULL) {
        printf("[msi  %9llu] core%d TOOK soft irq mepc=%llx\n",
               (unsigned long long)cyc, h,
               (unsigned long long)(h == 0 ? CORE_RAW(0, decode__DOT__mepc)
                                  : h == 1 ? CORE_RAW(1, decode__DOT__mepc)
                                  : h == 2 ? CORE_RAW(2, decode__DOT__mepc)
                                           : CORE_RAW(3, decode__DOT__mepc)));
        fflush(stdout);
      }
      mcause_prev[h] = mc[h];
    }

    if ((cyc % 10000000ULL) == 0) {
      printf("[prog %9llu] pcs=%llx/%llx/%llx/%llx\n", (unsigned long long)cyc,
             (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
             (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
      fflush(stdout);
    }

    if (cyc < next_snap) continue;
    next_snap += SNAP_STEP;

    printf("[snap %9llu] pcs=%llx/%llx/%llx/%llx\n", (unsigned long long)cyc,
           (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
           (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
    dump_cores();
    // Kernel ipi_data[] from the DRAM backing store. NOTE: values recently
    // AMO'd live dirty in an L1 — a 0 here can be stale; a set bit is real.
    for (int c = 0; c < 4; ++c) {
      uint64_t base = IPI_DATA_VA + (uint64_t)c * 128ULL;  // read_dram64 takes bus addr
      printf("        ipi_data[%d] bits=%llx stats={resched=%llu call=%llu}\n", c,
             (unsigned long long)bench.read_dram64(base + 64),
             (unsigned long long)bench.read_dram64(base + 0),
             (unsigned long long)bench.read_dram64(base + 8));
    }
    fflush(stdout);
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
