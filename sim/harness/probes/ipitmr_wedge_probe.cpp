// ipitmr_wedge_probe.cpp — run mt-ipitmr (or Linux) quietly and catch the
// exact wedge: a hart whose CLINT MSIP line stays asserted (pending IPI) for
// longer than THRESHOLD cycles has failed to take its machine-software trap.
// At that point dump every core's injection-FSM + branch/mem state ONCE and
// exit, so we see branchCounter / mExtensionReady / atomicBusyState / the
// frozen PC of the hart that is not servicing its IPI.
//
// Build: make build/ipitmr_wedge_probe.out
// Run  : build/ipitmr_wedge_probe.out bins/mt-ipitmr-q4.bin sim/data/qemu.dtb sim/data/boot.bin
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

  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("END",       200000000ULL);
  const uint64_t THRESHOLD  = env64("THRESHOLD",   300000ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  // per-hart: cycle at which MSIP most recently became 1 while still 1 now
  uint64_t msip_set_since[4] = {0, 0, 0, 0};
  uint32_t msip_prev[4]      = {0, 0, 0, 0};
  bool tx_prev[4] = {false, false, false, false};
  char linebuf[512]; int linelen = 0;
  uint64_t cyc = 0;

  auto robpc = [&](int n) -> unsigned long long {
    switch (n) { case 0: return tb_->robOut0_pc; case 1: return tb_->robOut1_pc;
                 case 2: return tb_->robOut2_pc; default: return tb_->robOut3_pc; }
  };
  auto dump_cores = [&]() {
    printf("        msip={%u,%u,%u,%u}\n",
           (unsigned)PERIPH(msipShared_0), (unsigned)PERIPH(msipShared_1),
           (unsigned)PERIPH(msipShared_2), (unsigned)PERIPH(msipShared_3));
#define ONE(n)                                                                \
    printf("        core%d pc=%llx injSt=%d injSoft=%d canIrq=%d canSoft=%d " \
           "brCnt=%d mExtRdy=%d extnMReq=%d atomBusy=%d "                     \
           "mstatus=%llx mie=%llx mcause=%llx mepc=%llx\n", n, robpc(n),      \
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
           (unsigned long long)CORE_RAW(n, decode__DOT__mepc));               \
    printf("             mem: coReq=%d inV=%d inAddr=%llx inWrV=%d "           \
           "rpV=%d rpAddr=%llx wbV=%d wbAddr=%llx resGuardBlk=%d "            \
           "resGuardCnt=%d staleUpg=%d\n",                                    \
           (int)CORE_RAW(n, memAccess__DOT__aceUnit_coherencyRequest_request_valid), \
           (int)CORE_RAW(n, memAccess__DOT__arbiter__DOT__inorderBuffer_valid),  \
           (unsigned long long)CORE_RAW(n, memAccess__DOT__arbiter__DOT__inorderBuffer_address), \
           (int)CORE_RAW(n, memAccess__DOT__arbiter__DOT__inorderBuffer_writeData_valid), \
           (int)CORE_RAW(n, memAccess__DOT__cacheLookup__DOT__replayBuffer_valid), \
           (unsigned long long)CORE_RAW(n, memAccess__DOT__cacheLookup__DOT__replayBuffer_address), \
           (int)CORE_RAW(n, memAccess__DOT__aceUnit__DOT__writeBuffer_valid),  \
           (unsigned long long)CORE_RAW(n, memAccess__DOT__aceUnit__DOT__writeBuffer_address), \
           (int)CORE_RAW(n, memAccess__DOT__cacheLookup__DOT__reservationGuardBlocked), \
           (int)CORE_RAW(n, memAccess__DOT__cacheLookup__DOT__reservationGuardCnt), \
           (int)CORE_RAW(n, memAccess__DOT__cacheLookup__DOT__isStaleUpgradeReplay))
    ONE(0); ONE(1); ONE(2); ONE(3);
#undef ONE
    // hart0 architectural registers relevant to the csd-clear loop:
    // a0=x10 a1=x11 a2=x12 a4=x14(s) a5=x15 a6=x16(bound=4) a7=x17 sp=x2 ra=x1
    printf("        hart0 regs: ra=%llx sp=%llx a0=%llx a1=%llx a2=%llx "
           "a4(s)=%llx a5=%llx a6(bound)=%llx a7=%llx | brEvalV=%d brEvalPass=%d "
           "brInstr=%llx brRob=%d\n",
           (unsigned long long)tb_->registersOut0_1,
           (unsigned long long)tb_->registersOut0_2,
           (unsigned long long)tb_->registersOut0_10,
           (unsigned long long)tb_->registersOut0_11,
           (unsigned long long)tb_->registersOut0_12,
           (unsigned long long)tb_->registersOut0_14,
           (unsigned long long)tb_->registersOut0_15,
           (unsigned long long)tb_->registersOut0_16,
           (unsigned long long)tb_->registersOut0_17,
           (int)CORE_RAW(0, branchEvals_valid),
           (int)CORE_RAW(0, branchEvals_passed),
           (unsigned long long)CORE_RAW(0, branchInstruction_instruction),
           (int)CORE_RAW(0, branchInstruction_robAddr));
    printf("        hart0 branch operands: brValid=%d rs1=%llx rs2=%llx nextPC=%llx\n",
           (int)CORE_RAW(0, branchInstruction_valid),
           (unsigned long long)CORE_RAW(0, branchInstruction_rs1),
           (unsigned long long)CORE_RAW(0, branchInstruction_rs2),
           (unsigned long long)CORE_RAW(0, branchEvals_nextPC));
  };

  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    // UART console capture (any port)
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
        } else if (c != '\r') { linebuf[linelen++] = c; }
      }
      tx_prev[p] = tx_v[p];
    }

    const uint32_t msip_cur[4] = {
        (uint32_t)PERIPH(msipShared_0), (uint32_t)PERIPH(msipShared_1),
        (uint32_t)PERIPH(msipShared_2), (uint32_t)PERIPH(msipShared_3)};
    for (int h = 0; h < 4; ++h) {
      if (msip_cur[h] == 1 && msip_prev[h] == 0) msip_set_since[h] = cyc;
      // Wedge test: pending IPI unserviced for > THRESHOLD cycles.
      if (msip_cur[h] == 1 && (cyc - msip_set_since[h]) > THRESHOLD) {
        printf("[WEDGE %9llu] hart%d MSIP pending for %llu cycles unserviced\n",
               (unsigned long long)cyc, h,
               (unsigned long long)(cyc - msip_set_since[h]));
        dump_cores();
        fflush(stdout);
        // Post-wedge trace: tick further and watch whether the wedged hart is
        // truly frozen (ROB not committing) or crawling, and whether its msip
        // toggles. Print per-hart commit counts + ROB occupancy each window.
        uint64_t commits[4] = {0,0,0,0};
        uint64_t msip1_lo = 99, msip1_hi = 0; // toggle range for the wedged hart
        for (uint64_t k = 0; k < 8000; ++k) {
          tb_->eval(); tb_->clock = 1; tb_->eval(); tb_->clock = 0; tb_->eval();
          ++cyc;
          // Use the top-level commitFired ports: the internal rob_commit_fired
          // net is not guaranteed to survive Verilator's optimiser.
          commits[0] += (uint64_t)tb_->robOut0_commitFired;
          commits[1] += (uint64_t)tb_->robOut1_commitFired;
          commits[2] += (uint64_t)tb_->robOut2_commitFired;
          commits[3] += (uint64_t)tb_->robOut3_commitFired;
          uint32_t m = (uint32_t)PERIPH(msipShared_0);
          if (h == 0) { if (m < msip1_lo) msip1_lo = m; if (m > msip1_hi) msip1_hi = m; }
          if ((k % 400) == 0) {
            printf("  +%4llu cyc=%llu pc0=%llx a4=%llx a6=%llx a1=%llx "
                   "msip0=%u robEmpty0=%d injSt0=%d canSoft0=%d brEvalV0=%d brInstr0=%llx\n",
                   (unsigned long long)k, (unsigned long long)cyc, robpc(0),
                   (unsigned long long)tb_->registersOut0_14,
                   (unsigned long long)tb_->registersOut0_16,
                   (unsigned long long)tb_->registersOut0_11,
                   (unsigned)PERIPH(msipShared_0),
                   (int)CORE_RAW(0, rob__DOT__fifo__DOT__emptyReg),
                   (int)CORE_RAW(0, interruptInjectStatus),
                   (int)CORE_RAW(0, decode_canTakeSoftInterrupt),
                   (int)CORE_RAW(0, branchEvals_valid),
                   (unsigned long long)CORE_RAW(0, branchInstruction_instruction));
            fflush(stdout);
          }
        }
        printf("[post-wedge] commits over 8000 cyc: c0=%llu c1=%llu c2=%llu c3=%llu; "
               "hart%d msip range [%llu..%llu]\n",
               (unsigned long long)commits[0], (unsigned long long)commits[1],
               (unsigned long long)commits[2], (unsigned long long)commits[3],
               h, (unsigned long long)msip1_lo, (unsigned long long)msip1_hi);
        fflush(stdout);
        return 7;
      }
      msip_prev[h] = msip_cur[h];
    }

    if ((cyc % 5000000ULL) == 0) {
      printf("[prog %9llu] pcs=%llx/%llx/%llx/%llx msip={%u,%u,%u,%u}\n",
             (unsigned long long)cyc, robpc(0), robpc(1), robpc(2), robpc(3),
             (unsigned)msip_cur[0], (unsigned)msip_cur[1],
             (unsigned)msip_cur[2], (unsigned)msip_cur[3]);
      fflush(stdout);
    }
  }
  printf("done cyc=%llu (no wedge)\n", (unsigned long long)cyc);
  return 0;
}
