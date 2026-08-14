// lrsc_wedge_probe.cpp — run mt-lrsc-q4 on the reservation-guard model and
// snapshot the LR/SC guard machinery around the phase-3 deadlock (~1.47M):
// per-core pc, reservationGuardCnt/Blocked, reservationRegister
// (reserved+address), arbiter atomicBusyState, the parked snoop in the ACE
// coherencyRequestBuffer, plus CCU FSM states — to see who is holding whom.
//
// Build:  make build/lrsc_wedge_probe.out
// Run  :  build/lrsc_wedge_probe.out bins/mt-lrsc-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CCU(sig) tb_->system__DOT__chiron__DOT__interconnect___DOT__CCU__DOT__##sig
#define CL(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__cacheLookup__DOT__##sig
#define ARB(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__arbiter__DOT__##sig
#define ACE(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__aceUnit__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-lrsc-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("LRSCP_END",  2100000ULL);
  const uint64_t SNAP_FROM = env64("LRSCP_FROM", 1300000ULL);
  const uint64_t SNAP_STEP = env64("LRSCP_STEP",   50000ULL);

  uint64_t cyc = 0, next_snap = SNAP_FROM;
  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < next_snap) continue;
    next_snap += SNAP_STEP;

    printf("[snap %8llu] pcs=%llx/%llx/%llx/%llx\n", (unsigned long long)cyc,
           (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
           (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
#define ONE(n)                                                                 \
    printf("  core%d guard(cnt=%3d blk=%d) resv(v=%d a=%08llx) atomBusy=%d "  \
           "snoop(v=%d a=%08llx r=%d)\n", n,                                   \
           (int)CL(n, reservationGuardCnt),                                    \
           (int)CL(n, reservationGuardBlocked),                                \
           (int)CL(n, reservationRegister_reserved),                           \
           (unsigned long long)CL(n, reservationRegister_address),             \
           (int)ARB(n, atomicBusyState),                                       \
           (int)ACE(n, coherencyRequestBuffer_valid),                          \
           (unsigned long long)ACE(n, coherencyRequestBuffer_address),         \
           (int)ACE(n, coherencyRequestBuffer_response))
    ONE(0); ONE(1); ONE(2); ONE(3);
#undef ONE
    printf("  ccu s={%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}\n",
           (int)CCU(stateReg_1), (int)CCU(stateReg_2), (int)CCU(stateReg_3),
           (int)CCU(stateReg_4), (int)CCU(stateReg_5), (int)CCU(stateReg_6),
           (int)CCU(stateReg_7), (int)CCU(stateReg_8), (int)CCU(stateReg_9),
           (int)CCU(stateReg_10), (int)CCU(stateReg_11), (int)CCU(stateReg_12));
    fflush(stdout);
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
