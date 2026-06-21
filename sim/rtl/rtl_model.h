// rtl_model.h — thin C++ driver around the Verilated core (Vsystem).
//
// Wraps the generated model with the few operations the lock-step harnesses
// need: bring-up + image load, single-instruction stepping (advance the clock
// until one instruction commits), and read-out of core 0's architectural state
// (the 32 GPRs + mstatus, exposed as registersOut0_0..32). Only core 0 is
// inspected here; the quad-core profiler reads all four cores directly.
#pragma once

#include <cstdio>
#include <cstdlib>
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
  unsigned long tickcount = 0;   // clocks advanced via step_nodump()
  unsigned      dump_tick = 0;   // clocks advanced via step() (VCD timeline)

  // Bring the core up and stream the image (+ dtb + boot ROM) into DRAM.
  void init(std::string image_name   = "Image",
            std::string dtb_name      = "qemu.dtb",
            std::string boot_rom      = "boot.bin",
            bool        enable_trace  = false,
            std::string trace_file    = "system_trace.vcd") {
    tb = new Vsystem;
    bind_registers();

    if (enable_trace) {
      Verilated::traceEverOn(true);
      tfp = new VerilatedVcdC;
      tb->trace(tfp, 99);
      tfp->open(trace_file.c_str());
    } else {
      tfp = nullptr;
    }

    // Reset: 20 cycles asserted, 20 released.
    tb->reset = 1;
    for (int i = 0; i < 20; ++i) tick_nodump();
    tb->reset = 0;
    for (int i = 0; i < 20; ++i) tick_nodump();

    printf("***** Loading kernel image *****\n");
    load_segment(image_name, 0x0UL);
    printf("loading dtb\n");
    load_segment(dtb_name, 0x07e00000UL);
    printf("loading boot rom\n");
    load_segment(boot_rom, 0x07ffff00UL);

    tb->finishedProgramming = 1;
    tb->programmer_valid    = 0;
    tick_nodump();
    tb->finishedProgramming = 0;
    tb->programmer_valid    = 0;
    tick_nodump();
    prev_pc = 0x80000000UL;
  }

  // Advance the clock until the next instruction commits.
  //   return 0 = committed, 1 = timed out, 2 = committed with interrupt.
  // step() drives the VCD timeline (dump_tick); step_nodump() does not
  // (tickcount). Both leave prev_pc at the committed instruction's PC.
  int step()        { return run_until_commit(/*dump=*/true);  }
  int step_nodump() { return run_until_commit(/*dump=*/false); }

  // ── core 0 register read-out (GPRs at 0..31, mstatus at 32) ───────────────
  uint64_t reg(int i) const { return (i >= 0 && i <= 32) ? *reg_[i] : 0; }
  uint64_t get_register_value(uint8_t rd) const { return reg(rd); }
  uint64_t read_register(int rs)          const { return reg(rs); }

  // First index (1..31, or 32 for mstatus) whose value disagrees with the
  // golden model; 0 if every register matches. x0 is not checked.
  int check_registers(const std::vector<uint64_t> &correct, uint64_t mstatus) const {
    for (int i = 1; i <= 31; ++i)
      if (reg(i) != correct[i]) return i;
    if (reg(32) != mstatus) return 32;
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

 private:
  Vsystem       *tb  = nullptr;
  VerilatedVcdC *tfp = nullptr;
  uint64_t      *reg_[33] = {};  // &registersOut0_0 .. _32, filled by init()

  void bind_registers() {
#define CHIRON_BIND(i) reg_[i] = &tb->registersOut0_##i;
    CHIRON_FOR_EACH_GPR(CHIRON_BIND)
#undef CHIRON_BIND
    reg_[32] = &tb->registersOut0_32;  // mstatus
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
  }

  int run_until_commit(bool dump) {
    advance(dump);
    for (int i = 0; !tb->robOut0_commitFired && i < STEP_TIMEOUT; ++i) {
#ifdef SHOW_TERMINAL
      if (tb->core0OutChar_valid) std::cout << tb->core0OutChar_byte << std::flush;
#endif
      advance(dump);
    }
    prev_pc = tb->robOut0_pc;
    if (tb->robOut0_interrupt && tb->robOut0_commitFired) return 2;
    if (tb->robOut0_commitFired) return 0;
    printf("TIMEOUT IN SIMULATOR!!!\n");
    return 1;
  }

  // Stream one file into DRAM at `base`, 8 bytes per programmer write.
  void load_segment(const std::string &path, unsigned long base) {
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> buffer(
        std::istreambuf_iterator<char>(input), {});

    tb->programmer_valid = 1;
    for (size_t i = 0; i + 8 <= buffer.size(); i += 8) {
      tb->programmer_byte   = *reinterpret_cast<unsigned long *>(&buffer[i]);
      tb->programmer_offset = base + i;
      tick_nodump();
      printf("Loaded: %zu %%\r", buffer.empty() ? (size_t)100 : (i * 100 / buffer.size()));
    }
    printf("done\n");
  }
};

#undef CHIRON_FOR_EACH_GPR
