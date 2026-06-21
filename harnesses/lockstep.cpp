// lockstep.cpp — unified lock-step harness (RTL vs golden emulator).
//
// Replaces the five near-identical lock_step_run_{vvadd,matmul,filter,csaxpy,
// histo}.cpp files, which differed only in a hardcoded image path and
// completion PC. Everything benchmark-specific is now a command-line argument,
// so the Makefile drives it from one manifest with no file copying.
//
// Usage:
//   ./lockstep.out --image <path>
//                  [--done-pc <hex> ...] [--done-a0 <val>]
//                  [--count-start-pc <hex>] [--count-stop-pc <hex>]
//                  [--logdir <dir>]
//
//   --image           RV64 image to load into BOTH back-ends (required in
//                     practice; defaults to emulator/src/Image).
//   --done-pc         Committed-PC that signals benchmark completion. Repeatable
//                     (a benchmark may exit from either of two stubs).
//   --done-a0         If given, completion also requires a0 (x10) == this value.
//   --count-start-pc  Optional: PC where the program-cycle window opens.
//   --count-stop-pc   Optional: PC where the program-cycle window closes.
//   --logdir          Directory for run.log / states.log / regs.log (default ".").
//
// Exit codes: 0 = completion reached (PASS), 1 = mismatch / timeout (FAIL).

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdint.h>
#define LOCKSTEP
#define MISA_SPEC (0b100000001000100000001 | (0b1llu << 63))
#include "emulator/src/emulator.h"
#undef SHOW_TERMINAL
#include "simulator/src/simulator.h"
#include <chrono>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <iomanip>
#include <time.h>
using namespace std;
using namespace std::chrono;

#define LOGGING
#define DUMP_CONDITION 0
#define PROBE_DOUBLE ((0xCA3BF0UL+(-136)) & (~7UL))

emulator golden_model;

struct keystroke_buffer {
  unsigned char reader, writer, char_buffer[128];
};

void signal_callback_handler(int signum) {
  golden_model.show_state(0);
  tcflush(0, TCIFLUSH);
  exit(signum);
}

// ── Argument parsing (same convention as profile.cpp) ─────────────────────
static const char *find_arg(int argc, char **argv, const char *flag,
                             const char *def = nullptr) {
  for (int i = 1; i < argc - 1; ++i)
    if (strcmp(argv[i], flag) == 0) return argv[i + 1];
  return def;
}
static vector<uint64_t> collect_hex_args(int argc, char **argv, const char *flag) {
  vector<uint64_t> out;
  for (int i = 1; i < argc - 1; ++i)
    if (strcmp(argv[i], flag) == 0) out.push_back(strtoull(argv[i + 1], nullptr, 0));
  return out;
}

int main(int argc, char* argv[]) {
  struct tm current_time;
  time_t now = time(NULL);
  localtime_r(&now, &current_time);

  // ── CLI ─────────────────────────────────────────────────────────────────
  const char *image_path = find_arg(argc, argv, "--image",
                                    "emulator/src/Image");
  const char *logdir     = find_arg(argc, argv, "--logdir", ".");
  vector<uint64_t> done_pcs = collect_hex_args(argc, argv, "--done-pc");
  const char *done_a0_str   = find_arg(argc, argv, "--done-a0", nullptr);
  bool     have_a0_cond     = (done_a0_str != nullptr);
  uint64_t done_a0_val      = have_a0_cond ? strtoull(done_a0_str, nullptr, 0) : 0;
  uint64_t count_start_pc   = strtoull(find_arg(argc, argv, "--count-start-pc", "0"), nullptr, 0);
  uint64_t count_stop_pc    = strtoull(find_arg(argc, argv, "--count-stop-pc",  "0"), nullptr, 0);

  string logp = string(logdir);
  if (!logp.empty() && logp.back() != '/') logp += "/";

  simulator bench;
  bench.init(image_path);  // dtb/bootrom use defaults (only the linux harness needs them)
  printf("bench inititated! image=%s\n", image_path);
  cout << endl;

  char x;
  golden_model.init(image_path);

  #ifdef LOGGING
  std::ofstream outFile((logp + "run.log").c_str());
  std::ofstream outState((logp + "states.log").c_str());
  std::ofstream outregs((logp + "regs.log").c_str());
  if (!outFile.is_open() || !outState.is_open() || !outregs.is_open()) {
    std::cerr << "Error opening a log file under " << logp << std::endl;
    return 1;
  }
  #endif

  std::vector<std::string> symbols;
  std::string line;
  unsigned long old_symbol = 1;
  unsigned long mem_address, data;

  // Optional program-cycle window (counts dump_ticks between two PCs)
  bool     window_open  = false;
  uint64_t count_start  = 0UL, count_stop = 0UL;

  printf("stepping\n");
  auto start = high_resolution_clock::now();
  int timer_interr = 0;
  signal(SIGINT, signal_callback_handler);

  keystroke_buffer keys_rx;
  keys_rx.reader = 0;
  keys_rx.writer = 0;
  unsigned long gprs[32];
  bench.set_probe(PROBE_DOUBLE);
  bench.step_nodump();
  unsigned long sim_prev = 0x80100000UL;
  printf("Runtime: %04d-%02d-%02d %02d:%02d:%02d\n",
    current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday,
    current_time.tm_hour, current_time.tm_min, current_time.tm_sec);

  while (1) {
    if (kbhit()) {
      keys_rx.char_buffer[keys_rx.writer++] = getchar();
      keys_rx.reader += (keys_rx.reader == keys_rx.writer);
      outFile << "keyhit\n";
    }

    #ifdef LOGGING
    outFile << setfill('0') << setw(16) << dec << (bench.dump_tick) << " ";
    outFile << setfill('0') << setw(16) << hex << golden_model.get_pc(0) << " ";
    outFile << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << endl;
    outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << endl;
    // golden_model.show_state(0);
    outregs << setfill('0') << setw(16) << hex << bench.return_instruction() << endl;
    outregs << bench.return_registers();
    outregs << "\n";
    #endif

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start);
    (void)duration; (void)timer_interr; (void)old_symbol; (void)mem_address;
    (void)data; (void)sim_prev; (void)gprs;

    // ── Lock-step state comparison (unchanged) ─────────────────────────────
    if (bench.prev_pc != golden_model.get_pc(0)) {
      cout << "PC mismatech emulator: " << hex << golden_model.get_pc(0);
      cout << " emulator instruction: " << setfill('0') << setw(8) << hex << golden_model.get_instruction(0);
      cout << " simulator: " << hex << bench.prev_pc << endl;
      golden_model.show_state(0);
      break;
    }
    if (bench.check_registers(golden_model.reg_file(0), golden_model.get_mstatus(0))) {
      cout << "Register mismatch at register " << dec << bench.check_registers(golden_model.reg_file(0), golden_model.get_mstatus(0));
      cout << " simulator value: " << setfill('0') << setw(16) << hex << bench.read_register(bench.check_registers(golden_model.reg_file(0), golden_model.get_mstatus(0))) << endl;
      golden_model.show_state(0);
      cout << dec << (bench.tickcount + bench.dump_tick) << endl;
      bench.step(); bench.step(); bench.step(); bench.step(); bench.step(); break;
    }
    sim_prev = golden_model.get_pc(0);
    int xx = 1;
    if (DUMP_CONDITION) { xx = bench.step(); } else { xx = bench.step_nodump(); }
    if (xx == 1) { break; }
    if (xx == 0) {
      golden_model.step();
      while (((golden_model.get_instruction(0) & 0x0000007f) == 0x73) &&
             (golden_model.get_instruction(0) & 0x00007000)) {
        golden_model.step();
      }
    }
    if (xx == 2) {
      if (DUMP_CONDITION) { xx = bench.step(); } else { xx = bench.step_nodump(); }
      if (xx == 1) { return 1; }
      if (golden_model.is_peripheral_read(0)) {
        golden_model.step();
      } else {
        golden_model.step();
      }
      while (((golden_model.get_instruction(0) & 0x0000007f) == 0x73) &&
             (golden_model.get_instruction(0) & 0x00007000)) {
        golden_model.step();
      }
    }

    // ── Optional program-cycle window ──────────────────────────────────────
    if (count_start_pc && bench.prev_pc == count_start_pc) { window_open = true;  count_start = bench.dump_tick; }
    else if (count_stop_pc && bench.prev_pc == count_stop_pc) { window_open = false; count_stop = bench.dump_tick; }

    // ── Completion check (replaces per-benchmark hardcoded PC) ─────────────
    bool done_hit = false;
    for (uint64_t dpc : done_pcs) if (bench.prev_pc == dpc) { done_hit = true; break; }
    if (done_hit && (!have_a0_cond || bench.get_register_value(10) == done_a0_val)) {
      printf("Test complete\n");
      if (count_start_pc && count_stop_pc && count_stop >= count_start)
        printf("Program cycles: %ld\n", (long)(count_stop - count_start));
      #ifdef LOGGING
      outFile.close();
      #endif
      tcflush(0, TCIFLUSH);
      return 0;  // PASS
    }
  }

  printf("Test failed: Time-out!\n");
  printf("Total ticks: %ld \n", (bench.tickcount + bench.dump_tick));
  golden_model.step();
  outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << endl;
  golden_model.step();
  outState << setfill('0') << setw(16) << hex << golden_model.get_instruction(0) << endl;

  #ifdef LOGGING
  outFile.close();
  #endif
  tcflush(0, TCIFLUSH);
  return 1;  // FAIL
}
