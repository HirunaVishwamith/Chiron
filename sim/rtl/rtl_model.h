// rtl_model.h — thin C++ driver around the Verilated core (Vsystem).
//
// Wraps the generated model with the few operations the lock-step harnesses
// need: bring-up + image load, single-instruction stepping (advance the clock
// until one instruction commits), and read-out of each core's architectural
// state (the 32 GPRs + mstatus, exposed as registersOutN_0..32). Core-0-only
// helpers remain for the ISA harnesses; multi-hart helpers support SMP Linux.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <stdint.h>

#include "Vsystem.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

// Image-loading chatter is harness output, not program output, so it goes to
// the debug sink (off unless a run passes --debug) rather than stdout. Without
// this a Linux boot log opens with four lines about DRAM offsets before the
// guest has executed a single instruction.
#include "sim/harness/common/simlog.h"

// Safety net: how many clocks to spin waiting for one commit before declaring a
// timeout. The makefile passes -DSTEP_TIMEOUT for trace builds; this is the
// fallback for anyone compiling the header standalone.
#ifndef STEP_TIMEOUT
#define STEP_TIMEOUT 1000
#endif

// core 0's register read-out ports registersOut0_0 .. _31 (the GPRs). mstatus
// is _32, bound separately. Listed once here, expanded wherever we need all 32.
#define CHIRON_FOR_EACH_GPR(X)                                                 \
  X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)                               \
  X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15)                             \
  X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23)                             \
  X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)

class simulator {
 public:
  uint64_t      prev_pc   = 0;   // PC of the most recently committed instruction
  int           prev_hart = 0;   // hart that produced prev_pc (multi-hart step)
  uint64_t      last_commit_pc[4] = {};  // per-hart PC of last commit seen
  unsigned long tickcount = 0;   // clocks advanced via step_nodump()
  unsigned      dump_tick = 0;   // clocks advanced via step() (VCD timeline)
  // Wall of simulated time. dump_tick only moves when VCD dumping is on, so
  // logs must use this sum — otherwise every run.log line looks like cycle 0.
  unsigned long cycles() const { return tickcount + dump_tick; }

  // Bring the core up and stream the image (+ dtb + boot ROM) into DRAM.
  void init(std::string image_name   = "Image",
            std::string dtb_name      = "qemu.dtb",
            std::string boot_rom      = "boot.bin",
            bool        enable_trace  = false,
            std::string trace_file    = "system_trace.vcd") {
    tb = new Vsystem;
    bind_registers();

    if (enable_trace) {
#ifdef CHIRON_NO_TRACE
      // This binary was linked against a model Verilated without --trace
      // (the fast linux_sim/profile path). VCD dumping is unavailable; use a
      // --trace build (the default `make sim` model) for waveforms instead.
      std::fprintf(stderr,
        "rtl_model: VCD tracing requested but this build has no trace support; "
        "ignoring.\n");
      tfp = nullptr;
#else
      Verilated::traceEverOn(true);
      tfp = new VerilatedVcdC;
      tb->trace(tfp, 99);
      tfp->open(trace_file.c_str());
#endif
    } else {
      tfp = nullptr;
    }

    // Reset: 20 cycles asserted, 20 released.
    tb->reset = 1;
    for (int i = 0; i < 20; ++i) tick_nodump();
    tb->reset = 0;
    for (int i = 0; i < 20; ++i) tick_nodump();

    SIMLOG("***** Loading kernel image *****\n");
    load_segment(image_name, 0x0UL);
    SIMLOG("loading dtb\n");
    load_segment(dtb_name, 0x07e00000UL);
    SIMLOG("loading boot rom\n");
    load_segment(boot_rom, 0x07ffff00UL);

    tb->finishedProgramming = 1;
    tb->programmer_valid    = 0;
    tick_nodump();
    tb->finishedProgramming = 0;
    tb->programmer_valid    = 0;
    tick_nodump();
    prev_pc = 0x80000000UL;
  }

  // Construct the model WITHOUT resetting it or loading an image.
  //
  // For restoring a Verilator checkpoint (--savable): the restore overwrites
  // every register and all 256 MB of DRAM, so resetting or loading here would
  // be wasted work at best and would corrupt the resumed state at worst. Pair
  // with VerilatedRestore; see sim/harness/probes/linux_ipi_probe.cpp.
  void init_no_image() {
    tb = new Vsystem;
    bind_registers();
    tfp = nullptr;
  }

  // Advance the clock until the next instruction commits.
  //   return 0 = committed, 1 = timed out, 2 = committed with interrupt.
  // step() drives the VCD timeline (dump_tick); step_nodump() does not
  // (tickcount). Both leave prev_pc at the committed instruction's PC.
  int step()        { return run_until_commit(/*dump=*/true);  }
  int step_nodump() { return run_until_commit(/*dump=*/false); }

  // Advance the clock until ANY core commits (0 = committed, 1 = timed out).
  // For RTL-only SMP runs (linux_sim): core 0 may legitimately sit in wfi as a
  // lottery loser while another hart does all the work, so gauging progress —
  // and declaring deadlock — on core 0 alone gives false stalls. Timeout here
  // means no core committed for STEP_TIMEOUT cycles: a genuine global wedge.
  int step_any_nodump() { return step_any(/*dump=*/false); }

  // Same as step_any_nodump(), but the caller picks per-call whether this
  // stretch of cycles is VCD-dumped. Lets a harness run undumped at full
  // speed and only flip dump=true once some runtime trigger fires (e.g. a
  // suspected wedge), instead of tracing from cycle 0 — see linux_sim_trace.cpp.
  int step_any(bool dump) {
    uint8_t mask = 0;
    int rc = step_until_commits(dump, &mask);
    return rc;
  }

  // Advance until at least one hart commits. On success, *mask has bit h set
  // for each hart that committed on that same cycle; last_commit_pc[h] and
  // prev_pc/prev_hart reflect those commits (prev_* = lowest-numbered hart).
  // Return: 0 = ok, 1 = timeout (no commit), 2 = at least one commit was an
  // interrupt retire (same encoding as run_until_commit for the lowest hart).
  int step_until_commits(bool dump, uint8_t *mask) {
    *mask = 0;
    for (int i = 0; i < STEP_TIMEOUT; ++i) {
      advance(dump);
      uint8_t m = 0;
      bool any_irq = false;
      for (int h = 0; h < 4; ++h) {
        if (!commit_fired(h)) continue;
        m |= (uint8_t)(1u << h);
        last_commit_pc[h] = core_pc(h);
        if (commit_interrupt(h)) any_irq = true;
      }
      if (m) {
        *mask = m;
        for (int h = 0; h < 4; ++h) {
          if (m & (1u << h)) {
            prev_hart = h;
            prev_pc = last_commit_pc[h];
            break;
          }
        }
        if (any_irq) return 2;
        return 0;
      }
    }
    prev_pc = tb->robOut0_pc;
    prev_hart = 0;
    printf("TIMEOUT IN SIMULATOR!!!\n");
    return 1;
  }

  int step_until_commits_nodump(uint8_t *mask) {
    return step_until_commits(/*dump=*/false, mask);
  }

  // robOut*_commitFired: CSR/SYSTEM retires suppressed (lockstep CSR-skips).
  bool commit_fired(int n) const {
    switch (n) {
      case 1:  return tb->robOut1_commitFired;
      case 2:  return tb->robOut2_commitFired;
      case 3:  return tb->robOut3_commitFired;
      default: return tb->robOut0_commitFired;
    }
  }

  // (A commit_fired_raw() accessor used to live here, reading the internal
  // core<N>.rob_commit_fired net. Verilator no longer emits that net -- it is
  // inlined away -- so the accessor stopped compiling. It had no callers, and
  // the top-level robOut<N>_commitFired port above carries the same signal, so
  // it is gone rather than re-pointed at a duplicate of commit_fired().)

  bool commit_interrupt(int n) const {
    switch (n) {
      case 1:  return tb->robOut1_interrupt;
      case 2:  return tb->robOut2_interrupt;
      case 3:  return tb->robOut3_interrupt;
      default: return tb->robOut0_interrupt;
    }
  }

  // PC of core n's most recently committed instruction (n = 0..3).
  uint64_t core_pc(int n) const {
    switch (n) {
      case 1:  return tb->robOut1_pc;
      case 2:  return tb->robOut2_pc;
      case 3:  return tb->robOut3_pc;
      default: return tb->robOut0_pc;
    }
  }

  // Live CLINT MSIP bit (msipShared[h](0)) as seen by the RTL cores.
  // Used by multi-hart lock-step to keep golden's IPI view matched to RTL
  // AXI write latency (golden stores would otherwise set MSIP instantly).
  uint32_t msip(int h) const {
    switch (h) {
      case 1:  return tb->system__DOT__peripherals__DOT__msipShared_1 & 1u;
      case 2:  return tb->system__DOT__peripherals__DOT__msipShared_2 & 1u;
      case 3:  return tb->system__DOT__peripherals__DOT__msipShared_3 & 1u;
      default: return tb->system__DOT__peripherals__DOT__msipShared_0 & 1u;
    }
  }

  // Live CLINT mtime (MultiUart free-running counter).
  uint64_t mtime() const {
    return tb->system__DOT__peripherals__DOT__mtime;
  }

  uint64_t mtimecmp(int h) const {
    switch (h) {
      case 1:  return tb->system__DOT__peripherals__DOT__mtimecmpShared_1;
      case 2:  return tb->system__DOT__peripherals__DOT__mtimecmpShared_2;
      case 3:  return tb->system__DOT__peripherals__DOT__mtimecmpShared_3;
      default: return tb->system__DOT__peripherals__DOT__mtimecmpShared_0;
    }
  }

  // Architectural mepc (decode CSR file). Timer force-take uses golden's
  // earlier PC as mepc; RTL may have interrupted a few instructions later.
  // Lock-step copies this so trap-entry CSR reads (csrr mepc → x18) match.
  uint64_t mepc(int h) const {
    switch (h) {
      case 1:  return tb->system__DOT__chiron__DOT__core1__DOT__decode__DOT__mepc;
      case 2:  return tb->system__DOT__chiron__DOT__core2__DOT__decode__DOT__mepc;
      case 3:  return tb->system__DOT__chiron__DOT__core3__DOT__decode__DOT__mepc;
      default: return tb->system__DOT__chiron__DOT__core0__DOT__decode__DOT__mepc;
    }
  }

  // Architectural mtvec (decode CSR file). The lock-step force-take window
  // must be exactly the trap-entry instructions at mtvec — a wide PC-range
  // heuristic misfires on ordinary kernel code that happens to live nearby
  // (e.g. arch_dup_task_struct at 0x8020206c vs handle_exception 0x80201b40).
  uint64_t mtvec(int h) const {
    switch (h) {
      case 1:  return tb->system__DOT__chiron__DOT__core1__DOT__decode__DOT__mtvec;
      case 2:  return tb->system__DOT__chiron__DOT__core2__DOT__decode__DOT__mtvec;
      case 3:  return tb->system__DOT__chiron__DOT__core3__DOT__decode__DOT__mtvec;
      default: return tb->system__DOT__chiron__DOT__core0__DOT__decode__DOT__mtvec;
    }
  }

  // Architectural mcause (decode CSR file). Lets the lock-step force-take
  // deliver the same interrupt cause the RTL actually took (timer vs MSIP
  // IPI), so the handler's later csrr mcause agrees on both sides.
  uint64_t mcause(int h) const {
    switch (h) {
      case 1:  return tb->system__DOT__chiron__DOT__core1__DOT__decode__DOT__mcause;
      case 2:  return tb->system__DOT__chiron__DOT__core2__DOT__decode__DOT__mcause;
      case 3:  return tb->system__DOT__chiron__DOT__core3__DOT__decode__DOT__mcause;
      default: return tb->system__DOT__chiron__DOT__core0__DOT__decode__DOT__mcause;
    }
  }

  // ── register read-out (GPRs at 0..31, mstatus at 32); hart 0..3 ───────────
  uint64_t reg(int hart, int i) const {
    if (hart < 0 || hart > 3 || i < 0 || i > 32) return 0;
    return *reg_[hart][i];
  }
  // Back-compat: bare reg(i) is hart 0.
  uint64_t reg(int i) const { return reg(0, i); }
  uint64_t get_register_value(uint8_t rd) const { return reg(0, rd); }
  uint64_t get_register_value(int hart, uint8_t rd) const { return reg(hart, rd); }
  uint64_t read_register(int rs)          const { return reg(0, rs); }
  uint64_t read_register(int hart, int rs) const { return reg(hart, rs); }

  // First index (1..31, or 32 for mstatus) whose value disagrees with the
  // golden model; 0 if every register matches. x0 is not checked.
  int check_registers(const std::vector<uint64_t> &correct, uint64_t mstatus) const {
    return check_registers(0, correct, mstatus);
  }
  int check_registers(int hart, const std::vector<uint64_t> &correct,
                      uint64_t mstatus) const {
    for (int i = 1; i <= 31; ++i)
      if (reg(hart, i) != correct[i]) return i;
    if (reg(hart, 32) != mstatus) return 32;
    return 0;
  }

  // Human-readable GPR dump (x0..x31), 8 per line — for the regs.log trace.
  std::string return_registers() const {
    std::string out;
    char buf[40];
    for (int i = 0; i < 32; ++i) {
      std::snprintf(buf, sizeof(buf), "x%-2d: %016lx%s", i,
                    (unsigned long)reg(i), (i % 8 == 7) ? "\n" : " ");
      out += buf;
    }
    return out;
  }

  int return_instruction() const { return tb->robOut0_pc; }

  // Memory-probe port (the lock-step harnesses point it at a known DRAM word).
  void          set_probe(unsigned long address) { tb->prober_offset = address; }
  unsigned long get_probe() const { return tb->prober_accessLong; }

  // Raw Verilated model — for ad-hoc debug probes that need hierarchical
  // signals rtl_model.h doesn't wrap. Read-only use intended.
  Vsystem *raw() { return tb; }

  // Read an aligned 64-bit word straight out of the Verilated DRAM backing
  // array (same array load_segment writes). This is the DRAM truth — it does
  // NOT see dirty lines still in the L1/L2 caches. Used by the lock-step
  // racy-fixup log to tell "memory corrupted by a bad writeback" (DRAM word
  // differs from golden) from "clean DRAM, fill-path returned wrong data".
  uint64_t read_dram64(uint64_t phys) const {
    constexpr size_t kDramBytes = sizeof(tb->system__DOT__memory__DOT__memory);
    if (phys < 0x80000000ULL) return 0;
    const uint64_t off = (phys - 0x80000000ULL) & ~7ULL;
    if (off + 8 > kDramBytes) return 0;
    uint64_t v;
    std::memcpy(&v, &tb->system__DOT__memory__DOT__memory[off], 8);
    return v;
  }

 private:
  Vsystem       *tb  = nullptr;
  VerilatedVcdC *tfp = nullptr;
  uint64_t      *reg_[4][33] = {};  // &registersOutN_0 .. _32 per hart
  bool          tx_prev_valid_[4] = {};  // edge-detect for SHOW_TERMINAL UART TX

  void bind_registers() {
#define CHIRON_BIND0(i) reg_[0][i] = &tb->registersOut0_##i;
#define CHIRON_BIND1(i) reg_[1][i] = &tb->registersOut1_##i;
#define CHIRON_BIND2(i) reg_[2][i] = &tb->registersOut2_##i;
#define CHIRON_BIND3(i) reg_[3][i] = &tb->registersOut3_##i;
    CHIRON_FOR_EACH_GPR(CHIRON_BIND0)
    CHIRON_FOR_EACH_GPR(CHIRON_BIND1)
    CHIRON_FOR_EACH_GPR(CHIRON_BIND2)
    CHIRON_FOR_EACH_GPR(CHIRON_BIND3)
#undef CHIRON_BIND0
#undef CHIRON_BIND1
#undef CHIRON_BIND2
#undef CHIRON_BIND3
    reg_[0][32] = &tb->registersOut0_32;
    reg_[1][32] = &tb->registersOut1_32;
    reg_[2][32] = &tb->registersOut2_32;
    reg_[3][32] = &tb->registersOut3_32;
  }

  // One clock with a VCD dump at this tick's timeline position.
  void tick(unsigned t) {
    tb->eval();
    if (tfp) tfp->dump(t * 10 - 2);
    tb->clock = 1; tb->eval();
    if (tfp) tfp->dump(t * 10);
    tb->clock = 0; tb->eval();
    if (tfp) { tfp->dump(t * 10 + 5); tfp->flush(); }
  }

  // One clock, no dump.
  void tick_nodump() {
    tb->eval();
    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
  }

  void advance(bool dump) {
    if (dump) tick(++dump_tick);
    else { ++tickcount; tick_nodump(); }
#ifdef SHOW_TERMINAL
    // Surface UART TX bytes once per write. Each core has its own uartPort, so
    // under SMP Linux console bytes can come from ANY of the four ports (the
    // kernel prints from whichever hart holds the console lock) — watch all
    // four, not just core 0. valid is held high while the write request is
    // buffered, so edge-detect per port to print each byte once.
    const bool tx_v[4] = {tb->core0OutChar_valid != 0, tb->core1OutChar_valid != 0,
                          tb->core2OutChar_valid != 0, tb->core3OutChar_valid != 0};
    const char tx_b[4] = {(char)tb->core0OutChar_byte, (char)tb->core1OutChar_byte,
                          (char)tb->core2OutChar_byte, (char)tb->core3OutChar_byte};
    for (int p = 0; p < 4; ++p) {
      if (tx_v[p] && !tx_prev_valid_[p]) std::cout << tx_b[p] << std::flush;
      tx_prev_valid_[p] = tx_v[p];
    }
#endif
  }

  int run_until_commit(bool dump) {
    advance(dump);
    for (int i = 0; !tb->robOut0_commitFired && i < STEP_TIMEOUT; ++i) {
      advance(dump);
    }
    prev_pc = tb->robOut0_pc;
    if (tb->robOut0_interrupt && tb->robOut0_commitFired) return 2;
    if (tb->robOut0_commitFired) return 0;
    printf("TIMEOUT IN SIMULATOR!!!\n");
    return 1;
  }

  // Stream one file into DRAM at `base`.
  //
  // The RTL exposes a clocked 64-bit "programmer" port, but driving it one word
  // per clock means ~one full-design Verilator eval per 8 bytes — for a multi-MB
  // Linux image that is ~10^6 evals, i.e. minutes of wall time spent just
  // loading. Instead we write straight into the Verilated DRAM backing array
  // (a flat little-endian byte image, identical in layout to the .bin on disk),
  // which is effectively instantaneous. init() still pulses finishedProgramming
  // afterwards so the model's `programmed` latch is set the normal way.
  void load_segment(const std::string &path, unsigned long base) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      std::fprintf(stderr, "load_segment: cannot open '%s'\n", path.c_str());
      std::exit(1);
    }
    std::vector<unsigned char> buffer(
        std::istreambuf_iterator<char>(input), {});

    constexpr size_t kDramBytes =
        sizeof(tb->system__DOT__memory__DOT__memory);
    if (base + buffer.size() > kDramBytes) {
      std::fprintf(stderr,
        "load_segment: '%s' (%zu bytes) @ 0x%lx overflows DRAM (0x%zx bytes)\n",
        path.c_str(), buffer.size(), base, kDramBytes);
      std::exit(1);
    }
    std::memcpy(&tb->system__DOT__memory__DOT__memory[base],
                buffer.data(), buffer.size());
    SIMLOG("loaded %zu bytes @ 0x%08lx (%s)\n",
           buffer.size(), base, path.c_str());
  }
};

#undef CHIRON_FOR_EACH_GPR
