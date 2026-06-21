// lockstep.cpp — unified lock-step harness (RTL vs golden emulator).
//
// Replaces the five near-identical per-benchmark harnesses; everything
// benchmark-specific is a command-line argument driven from the manifest.
//
// Usage:
//   ./lockstep.out --image <path>
//                  [--done-pc <hex> ...]   committed-PC that signals completion
//                  [--done-a0 <val>]        completion also requires a0 == val
//                  [--count-start-pc <hex>] open program-cycle window at this PC
//                  [--count-stop-pc  <hex>] close program-cycle window at this PC
//                  [--logdir <dir>]         destination for run/states/regs logs
//                  [--show-state]           print golden-model state each step
//                  [--dump-waves]           write VCD to <logdir>/system_trace.vcd
//
// Exit codes: 0 = completion reached (PASS), 1 = mismatch / timeout (FAIL).

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdint.h>
#include <iomanip>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <termios.h>

#define LOCKSTEP
#define MISA_SPEC (0b100000001000100000001 | (0b1llu << 63))
#include "emulator/src/emulator.h"
#undef SHOW_TERMINAL
#include "simulator/src/simulator.h"

using namespace std;

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

// ── Argument parsing ──────────────────────────────────────────────────────────
static const char *find_arg(int argc, char **argv, const char *flag,
                             const char *def = nullptr) {
  for (int i = 1; i < argc - 1; ++i)
    if (strcmp(argv[i], flag) == 0) return argv[i + 1];
  return def;
}

static bool has_flag(int argc, char **argv, const char *flag) {
  for (int i = 1; i < argc; ++i)
    if (strcmp(argv[i], flag) == 0) return true;
  return false;
}

static vector<uint64_t> collect_hex_args(int argc, char **argv, const char *flag) {
  vector<uint64_t> out;
  for (int i = 1; i < argc - 1; ++i)
    if (strcmp(argv[i], flag) == 0)
      out.push_back(strtoull(argv[i + 1], nullptr, 0));
  return out;
}

int main(int argc, char* argv[]) {
  struct tm current_time;
  time_t now = time(NULL);
  localtime_r(&now, &current_time);

  // ── CLI ───────────────────────────────────────────────────────────────────
  const char *image_path  = find_arg(argc, argv, "--image", "emulator/src/Image");
  const char *logdir      = find_arg(argc, argv, "--logdir", ".");
  bool show_state         = has_flag(argc, argv, "--show-state");
  bool dump_waves         = has_flag(argc, argv, "--dump-waves");

  vector<uint64_t> done_pcs = collect_hex_args(argc, argv, "--done-pc");
  const char *done_a0_str   = find_arg(argc, argv, "--done-a0", nullptr);
  bool     have_a0_cond     = (done_a0_str != nullptr);
  uint64_t done_a0_val      = have_a0_cond ? strtoull(done_a0_str, nullptr, 0) : 0;
  uint64_t count_start_pc   = strtoull(find_arg(argc, argv, "--count-start-pc", "0"), nullptr, 0);
  uint64_t count_stop_pc    = strtoull(find_arg(argc, argv, "--count-stop-pc",  "0"), nullptr, 0);

  string logp = string(logdir);
  if (!logp.empty() && logp.back() != '/') logp += "/";

  string vcd_path = logp + "system_trace.vcd";

  // ── Init RTL + golden model ───────────────────────────────────────────────
  simulator bench;
  bench.init(image_path, "qemu.dtb", "boot.bin", dump_waves, vcd_path);
  printf("bench initiated! image=%s\n", image_path);
  if (dump_waves) printf("[lockstep] VCD → %s\n", vcd_path.c_str());
  cout << endl;

  golden_model.init(image_path);

  std::ofstream outFile((logp + "run.log").c_str());
  std::ofstream outState((logp + "states.log").c_str());
  std::ofstream outregs((logp + "regs.log").c_str());
  if (!outFile.is_open() || !outState.is_open() || !outregs.is_open()) {
    std::cerr << "Error opening log files under " << logp << std::endl;
    return 1;
  }

  uint64_t count_start = 0UL, count_stop = 0UL;

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
    int xx = dump_waves ? bench.step() : bench.step_nodump();
    if (xx == 1) { break; }
    if (xx == 0) {
      golden_model.step();
      while (((golden_model.get_instruction(0) & 0x7f) == 0x73) &&
              (golden_model.get_instruction(0) & 0x7000))
        golden_model.step();
    }
    if (xx == 2) {
      xx = dump_waves ? bench.step() : bench.step_nodump();
      if (xx == 1) { return 1; }
      golden_model.step();
      while (((golden_model.get_instruction(0) & 0x7f) == 0x73) &&
              (golden_model.get_instruction(0) & 0x7000))
        golden_model.step();
    }

    // ── Optional program-cycle window ─────────────────────────────────────
    if (count_start_pc && bench.prev_pc == count_start_pc) count_start = bench.dump_tick;
    else if (count_stop_pc  && bench.prev_pc == count_stop_pc)  count_stop  = bench.dump_tick;

    // ── Completion check ──────────────────────────────────────────────────
    bool done_hit = false;
    for (uint64_t dpc : done_pcs) if (bench.prev_pc == dpc) { done_hit = true; break; }
    if (done_hit && (!have_a0_cond || bench.get_register_value(10) == done_a0_val)) {
      printf("Test complete\n");
      if (count_start_pc && count_stop_pc && count_stop >= count_start)
        printf("Program cycles: %ld\n", (long)(count_stop - count_start));
      outFile.close();
      tcflush(0, TCIFLUSH);
      return 0;
    }
  }

  printf("Test failed: Time-out!\n");
  printf("Total ticks: %ld\n", (bench.tickcount + bench.dump_tick));
  golden_model.step();
  outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << "\n";
  golden_model.step();
  outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << "\n";

  outFile.close();
  tcflush(0, TCIFLUSH);
  return 1;
}
