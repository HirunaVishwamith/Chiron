// inject_fsm_probe.cpp — trace the interrupt-injection FSM of whichever hart
// wedges, not just hart0.
//
// mt-ipimux wedges at ~27.1M with hart1 showing injSt=1 (waitToInjectInterr),
// canSoft=1, branchCounter=1 and its MSIP pending forever. The FSM
// (core.scala:1160-1183) can only leave waitToInjectInterr two ways:
//   * branchCounter == 0  -> inject the synthetic system instruction; or
//   * branchCounter != 0 AND a CONDITIONAL branch (opcode 1100011) is issuing
//     this cycle -> synthetic mispredict, flush, then inject.
// branchCounter however counts every opcode(6,4)==110 instruction, i.e. JAL and
// JALR as well as BRANCH (core.scala:1105). Meanwhile core.scala:1197 freezes
// fetch for the entire time injSt != waitForMTIP. So if branchCounter is held
// nonzero by something that never presents itself as a conditional branch, the
// hart can sit in waitToInjectInterr forever and never take its IPI.
//
// This probe finds the hart whose MSIP stays asserted past THRESHOLD, then dumps
// a cycle-by-cycle window of exactly the signals that decide the FSM's next
// state, so we can see whether branchCounter is frozen and what (if anything) is
// issuing as a branch.
//
// Build: make build/inject_fsm_probe.out
// Run  : build/inject_fsm_probe.out bins/mt-ipimux-q4.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define PERIPH(sig) tb_->system__DOT__peripherals__DOT__##sig
#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-ipimux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END       = env64("END", 200000000ULL);
  const uint64_t THRESHOLD = env64("THRESHOLD", 300000ULL);
  const uint64_t WINDOW    = env64("WINDOW", 3000ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  uint64_t msip_since[4] = {0,0,0,0};
  uint32_t msip_prev[4]  = {0,0,0,0};
  uint64_t cyc = 0;
  int wedged = -1;

  auto msip = [&](int h) -> uint32_t {
    switch (h) { case 0: return PERIPH(msipShared_0); case 1: return PERIPH(msipShared_1);
                 case 2: return PERIPH(msipShared_2); default: return PERIPH(msipShared_3); }
  };
  auto robpc = [&](int n) -> unsigned long long {
    switch (n) { case 0: return tb_->robOut0_pc; case 1: return tb_->robOut1_pc;
                 case 2: return tb_->robOut2_pc; default: return tb_->robOut3_pc; }
  };

  // Per-hart accessors for the FSM-deciding signals (the macro can only be
  // instantiated with a literal core index, hence the switch).
#define PICK(sig) \
  [&](int n) -> unsigned long long { \
    switch (n) { case 0: return (unsigned long long)CORE_RAW(0, sig); \
                 case 1: return (unsigned long long)CORE_RAW(1, sig); \
                 case 2: return (unsigned long long)CORE_RAW(2, sig); \
                 default: return (unsigned long long)CORE_RAW(3, sig); } }
  auto injSt    = PICK(interruptInjectStatus);
  auto injSoft  = PICK(injectingSoftwareInterrupt);
  auto brCnt    = PICK(branchCounter);
  auto biValid  = PICK(branchInstruction_valid);
  auto biInsn   = PICK(branchInstruction_instruction);
  auto beValid  = PICK(branchEvals_valid);
  auto bePassed = PICK(branchEvals_passed);
  auto canSoft  = PICK(decode_canTakeSoftInterrupt);
  auto canIrq   = PICK(decode_canTakeInterrupt);
  auto lastSys  = PICK(lastRetiredSystem);
  auto mstatus  = PICK(decode__DOT__mstatus);
  auto mie      = PICK(decode__DOT__mie);
#undef PICK

  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    for (int h = 0; h < 4; ++h) {
      const uint32_t m = msip(h);
      if (m && !msip_prev[h]) msip_since[h] = cyc;
      msip_prev[h] = m;
      if (m && msip_since[h] && cyc - msip_since[h] > THRESHOLD && wedged < 0) wedged = h;
    }
    if (wedged >= 0) break;
  }

  if (wedged < 0) { printf("no wedge by cyc=%llu\n", (unsigned long long)cyc); return 0; }

  printf("[WEDGE %llu] hart%d MSIP pending %llu cycles unserviced\n\n",
         (unsigned long long)cyc, wedged, (unsigned long long)(cyc - msip_since[wedged]));
  printf("tracing hart%d for %llu cycles\n", wedged, (unsigned long long)WINDOW);
  printf("%-11s %-10s %-4s %-5s %-6s %-10s %-9s %-8s %-9s %s\n",
         "cyc", "pc", "inj", "soft", "brCnt", "bi.valid/insn", "be.v/pass",
         "canS/canI", "lastSys", "mstatus/mie");

  uint64_t prev_sig = ~0ULL; int printed = 0;
  for (uint64_t i = 0; i < WINDOW; ++i) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    const int h = wedged;
    // Print on change of the decision-relevant state, plus a periodic heartbeat,
    // so a frozen FSM is obvious rather than buried in identical lines.
    const uint64_t sig = (injSt(h) << 40) ^ (brCnt(h) << 24) ^ (biValid(h) << 20) ^
                         (beValid(h) << 16) ^ (biInsn(h) & 0xffff);
    if (sig != prev_sig || (i % 500) == 0) {
      printf("%-11llu %-10llx %-4llu %-5llu %-6llu %llu/%08llx  %llu/%llu       %llu/%llu       %llu        %llx/%llx\n",
             (unsigned long long)cyc, robpc(h), injSt(h), injSoft(h), brCnt(h),
             biValid(h), biInsn(h), beValid(h), bePassed(h),
             canSoft(h), canIrq(h), lastSys(h), mstatus(h), mie(h));
      if (++printed > 400) break;
    }
    prev_sig = sig;
  }
  return 0;
}
