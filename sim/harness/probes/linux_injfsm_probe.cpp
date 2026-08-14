// linux_injfsm_probe.cpp — inject_fsm_probe.cpp adapted to RESTORE from a
// linux_ipi_probe checkpoint instead of cold-booting, so a wedge discovered
// deep into a Linux SMP boot (hours of cold-boot cost) can be re-approached in
// minutes.
//
// 2026-08-12: hart0 froze at pc=81b03840 for 80M+ cycles during a quad-core
// Linux boot on the (fixed, post-dual-Unique) RTL, with CLINT msip_line=1 for
// hart0 held the entire time and mip.MSIP reading back 0 — the exact
// unserviced-IPI signature inject_fsm_probe.cpp was built to dissect (see its
// header comment: mt-ipimux wedged the same way via branchCounter staying
// nonzero forever in waitToInjectInterr). This probe restores
// ckpt_001200000000.bin (cycle 1,200,000,000, ~40M cycles before the freeze
// began) and traces hart0's FSM decision signals through the transition.
//
// Build: make build/linux_injfsm_probe.out
// Run:   CKPT_RESTORE=ckpt/ckpt_001200000000.bin HART=0 \
//          ./build/linux_injfsm_probe.out
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

#define PERIPH(sig) tb_->system__DOT__peripherals__DOT__##sig
#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig

int main() {
  const char *ckpt = getenv("CKPT_RESTORE");
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }

  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END       = env64("END", 100000000ULL);   // cycles to run past restore
  const uint64_t THRESHOLD = env64("THRESHOLD", 300000ULL);
  const uint64_t WINDOW    = env64("WINDOW", 5000ULL);
  const int FORCE_HART     = getenv("HART") ? atoi(getenv("HART")) : -1;

  simulator bench;
  bench.init_no_image();
  Vsystem *tb_ = bench.raw();

  uint64_t cyc = 0, msip_writes[4] = {}, last_uart_cyc = 0, uart_bytes = 0;
  {
    VerilatedRestore rs;
    rs.open(ckpt);
    if (!rs.isOpen()) { std::fprintf(stderr, "cannot open %s\n", ckpt); return 1; }
    rs >> cyc;
    rs >> msip_writes[0]; rs >> msip_writes[1];
    rs >> msip_writes[2]; rs >> msip_writes[3];
    rs >> last_uart_cyc;  rs >> uart_bytes;
    rs >> *tb_; rs.close();
  }
  const uint64_t start = cyc, deadline = cyc + END;
  std::fprintf(stderr, "restored at %llu, running to %llu\n",
               (unsigned long long)start, (unsigned long long)deadline);

  uint64_t msip_since[4] = {0,0,0,0};
  uint32_t msip_prev[4]  = {0,0,0,0};
  int wedged = -1;

  auto msip = [&](int h) -> uint32_t {
    switch (h) { case 0: return PERIPH(msipShared_0); case 1: return PERIPH(msipShared_1);
                 case 2: return PERIPH(msipShared_2); default: return PERIPH(msipShared_3); }
  };
  auto robpc = [&](int n) -> unsigned long long {
    switch (n) { case 0: return tb_->robOut0_pc; case 1: return tb_->robOut1_pc;
                 case 2: return tb_->robOut2_pc; default: return tb_->robOut3_pc; }
  };

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

  // Ring buffer of branchCounter transitions per hart. The FSM only wedges when
  // an interrupt finally needs injecting, but the branchCounter leak that traps
  // it can happen ARBITRARILY EARLIER — so the interesting event is the LAST
  // transition before the wedge, which a forward-only trace started at the wedge
  // can never show. Keep the recent history and dump it on detection.
  struct BrEvt { uint64_t cyc, pc; unsigned br, inj, beV, beP, biV; uint64_t biI; };
  const int RING = 64;
  std::vector<std::vector<BrEvt>> ring(4);
  std::vector<int> ringN(4, 0);
  for (int h = 0; h < 4; ++h) ring[h].resize(RING);
  unsigned long long brPrev[4] = {~0ULL, ~0ULL, ~0ULL, ~0ULL};

  uint64_t lastReport = cyc;
  while (cyc < deadline) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    for (int h = 0; h < 4; ++h) {
      const unsigned long long b = brCnt(h);
      if (b != brPrev[h]) {
        BrEvt &e = ring[h][ringN[h] % RING];
        e.cyc = cyc; e.pc = robpc(h); e.br = (unsigned)b; e.inj = (unsigned)injSt(h);
        e.beV = (unsigned)beValid(h); e.beP = (unsigned)bePassed(h);
        e.biV = (unsigned)biValid(h); e.biI = biInsn(h);
        ringN[h]++;
        brPrev[h] = b;
      }
    }

    for (int h = 0; h < 4; ++h) {
      const uint32_t m = msip(h);
      if (m && !msip_prev[h]) msip_since[h] = cyc;
      msip_prev[h] = m;
      if (FORCE_HART < 0 && m && msip_since[h] && cyc - msip_since[h] > THRESHOLD && wedged < 0)
        wedged = h;
    }
    if (FORCE_HART >= 0 && cyc - start > THRESHOLD) { wedged = FORCE_HART; }
    if (wedged >= 0) break;

    if (cyc - lastReport >= 5000000) {
      lastReport = cyc;
      std::fprintf(stderr, "  [%llu] msip_since=%llu,%llu,%llu,%llu\n",
                   (unsigned long long)cyc, (unsigned long long)msip_since[0],
                   (unsigned long long)msip_since[1], (unsigned long long)msip_since[2],
                   (unsigned long long)msip_since[3]);
    }
  }

  if (wedged < 0) { std::printf("no wedge by cyc=%llu\n", (unsigned long long)cyc); return 0; }

  std::printf("[WEDGE %llu] hart%d MSIP pending %llu cycles unserviced\n\n",
              (unsigned long long)cyc, wedged,
              (unsigned long long)(cyc - (msip_since[wedged] ? msip_since[wedged] : start)));
  // The decisive evidence: when did branchCounter last move, and what was
  // happening then? A leaked count shows up as an increment with no matching
  // decrement, often thousands or millions of cycles before the hart wedges.
  {
    const int h = wedged;
    const int n = ringN[h] < RING ? ringN[h] : RING;
    const int first = ringN[h] < RING ? 0 : ringN[h] % RING;
    std::printf("branchCounter transitions for hart%d (last %d, oldest first):\n", h, n);
    std::printf("%-13s %-10s %-6s %-4s %-9s %-6s %s\n",
                "cyc", "pc", "brCnt", "inj", "be.v/pass", "bi.v", "bi.insn");
    for (int i = 0; i < n; ++i) {
      const BrEvt &e = ring[h][(first + i) % RING];
      std::printf("%-13llu %-10llx %-6u %-4u %u/%-7u %-6u %08llx\n",
                  (unsigned long long)e.cyc, (unsigned long long)e.pc, e.br, e.inj,
                  e.beV, e.beP, e.biV, (unsigned long long)e.biI);
    }
    std::printf("  (cycles since last transition: %llu)\n\n",
                (unsigned long long)(n ? cyc - ring[h][(first + n - 1) % RING].cyc : 0));
  }

  std::printf("tracing hart%d for %llu cycles\n", wedged, (unsigned long long)WINDOW);
  std::printf("%-11s %-10s %-4s %-5s %-6s %-10s %-9s %-8s %-9s %s\n",
              "cyc", "pc", "inj", "soft", "brCnt", "bi.valid/insn", "be.v/pass",
              "canS/canI", "lastSys", "mstatus/mie");

  uint64_t prev_sig = ~0ULL; int printed = 0;
  for (uint64_t i = 0; i < WINDOW; ++i) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    const int h = wedged;
    const uint64_t sig = (injSt(h) << 40) ^ (brCnt(h) << 24) ^ (biValid(h) << 20) ^
                         (beValid(h) << 16) ^ (biInsn(h) & 0xffff);
    if (sig != prev_sig || (i % 500) == 0) {
      std::printf("%-11llu %-10llx %-4llu %-5llu %-6llu %llu/%08llx  %llu/%llu       %llu/%llu       %llu        %llx/%llx\n",
             (unsigned long long)cyc, robpc(h), injSt(h), injSoft(h), brCnt(h),
             biValid(h), biInsn(h), beValid(h), bePassed(h),
             canSoft(h), canIrq(h), lastSys(h), mstatus(h), mie(h));
      if (++printed > 600) break;
    }
    prev_sig = sig;
  }
  return 0;
}
