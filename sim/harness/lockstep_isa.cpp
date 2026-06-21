// lockstep_isa.cpp — lock-step harness for RISC-V ISA regression images.
//
// Loads a riscv-tests ELF image (bare-metal), runs it lock-step against the
// golden emulator, and exits with PASS (0) when gp==1 && a7==93.
//
// Usage:
//   ./lockstep_isa.out --image <path>
//                      [--logdir <dir>]    destination for run/states/regs logs
//                      [--show-state]      print golden-model state each step
//                      [--dump-waves]      write VCD to <logdir>/system_trace.vcd
//
// Exit codes: 0 = pass, 1 = fail / mismatch / timeout.

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <iomanip>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <termios.h>

#define LOCKSTEP
#define MISA_SPEC (0b100000001000100000001 | (0b1llu << 63))
#include "sim/emulator/emulator.h"
#undef SHOW_TERMINAL
#include "sim/rtl/rtl_model.h"

#include "sim/harness/common/args.h"
#include "sim/harness/common/completion.h"

using namespace std;
using namespace harness;

#define PROBE_DOUBLE ((0xCA3BF0UL + (-136)) & (~7UL))

emulator golden_model;

struct keystroke_buffer {
  unsigned char reader, writer, char_buffer[128];
};

void signal_callback_handler(int signum) {
  golden_model.show_state(0);
  tcflush(0, TCIFLUSH);
  exit(signum);
}

int main(int argc, char* argv[]) {
  struct tm current_time;
  time_t now = time(NULL);
  localtime_r(&now, &current_time);

  // ── CLI ───────────────────────────────────────────────────────────────────
  const char *image_path = find_arg(argc, argv, "--image", "sim/data/Image");
  const char *logdir     = find_arg(argc, argv, "--logdir", ".");
  bool show_state        = has_flag(argc, argv, "--show-state");
  bool dump_waves        = has_flag(argc, argv, "--dump-waves");

  string logp = string(logdir);
  if (!logp.empty() && logp.back() != '/') logp += "/";

  string vcd_path = logp + "system_trace.vcd";

  // ── Init RTL + golden model ───────────────────────────────────────────────
  simulator bench;
  bench.init(image_path, "sim/data/qemu.dtb", "sim/data/boot.bin", dump_waves, vcd_path);
  printf("bench initiated! image=%s\n", image_path);
  cout << endl;

  golden_model.init(image_path);

  std::ofstream outFile((logp + "run.log").c_str());
  std::ofstream outState((logp + "states.log").c_str());
  std::ofstream outregs((logp + "regs.log").c_str());
  if (!outFile.is_open() || !outState.is_open() || !outregs.is_open()) {
    std::cerr << "Error opening log files under " << logp << std::endl;
    return 1;
  }

  printf("stepping\n");
  signal(SIGINT, signal_callback_handler);

  keystroke_buffer keys_rx = {};
  bench.set_probe(PROBE_DOUBLE);
  bench.step_nodump();

  printf("Runtime: %04d-%02d-%02d %02d:%02d:%02d\n",
    current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday,
    current_time.tm_hour, current_time.tm_min, current_time.tm_sec);

  // ── Main lock-step loop ───────────────────────────────────────────────────
  while (1) {
    if (kbhit()) {
      keys_rx.char_buffer[keys_rx.writer++] = getchar();
      keys_rx.reader += (keys_rx.reader == keys_rx.writer);
      outFile << "keyhit\n";
    }

    outFile  << setfill('0') << setw(16) << dec << bench.dump_tick << " "
             << setfill('0') << setw(16) << hex << golden_model.get_pc(0) << " "
             << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << "\n";
    outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << "\n";
    if (show_state) golden_model.show_state(0);
    outregs  << setfill('0') << setw(16) << hex << bench.return_instruction() << "\n"
             << bench.return_registers() << "\n";

    // ── State comparison ──────────────────────────────────────────────────
    if (bench.prev_pc != golden_model.get_pc(0)) {
      cout << "PC mismatch  emulator: " << hex << golden_model.get_pc(0)
           << "  insn: " << setfill('0') << setw(8) << hex << golden_model.get_instruction(0)
           << "  simulator: " << hex << bench.prev_pc << "\n";
      golden_model.show_state(0);
      break;
    }
    int bad = bench.check_registers(golden_model.reg_file(0), golden_model.get_mstatus(0));
    if (bad) {
      cout << "Register mismatch x" << dec << bad
           << "  simulator value: " << setfill('0') << setw(16) << hex
           << bench.read_register(bad) << "\n";
      golden_model.show_state(0);
      cout << dec << (bench.tickcount + bench.dump_tick) << "\n";
      bench.step(); bench.step(); bench.step(); bench.step(); bench.step();
      break;
    }

    // ── Advance both models ───────────────────────────────────────────────
    int x = dump_waves ? bench.step() : bench.step_nodump();
    if (x == 1) { break; }
    if (x == 0) {
      golden_model.step();
      while (((golden_model.get_instruction(0) & 0x7f) == 0x73) &&
              (golden_model.get_instruction(0) & 0x7000))
        golden_model.step();
    }
    if (x == 2) {
      x = dump_waves ? bench.step() : bench.step_nodump();
      if (x == 1) { return 1; }
      golden_model.step();
      while (((golden_model.get_instruction(0) & 0x7f) == 0x73) &&
              (golden_model.get_instruction(0) & 0x7000))
        golden_model.step();
    }

    // ── ISA test completion (gp/x3 == 1, a7/x17 == 93) ───────────────────
    IsaResult isa = isa_test_status(bench.read_register(3), bench.read_register(17));
    if (isa == IsaResult::Pass) {
      printf("Test complete\n");
      outFile.close();
      tcflush(0, TCIFLUSH);
      return 0;
    }
    if (isa == IsaResult::Fail) {
      printf("Test Failed\n");
      outFile.close();
      tcflush(0, TCIFLUSH);
      return 1;
    }
  }

  printf("Test failed: Time-out!\n");
  printf("Total ticks: %ld\n", (bench.tickcount + bench.dump_tick));
  golden_model.step();
  outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << "\n";
  golden_model.show_state(0);
  golden_model.step();
  outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << "\n";
  golden_model.show_state(0);

  outFile.close();
  tcflush(0, TCIFLUSH);
  return 1;
}
