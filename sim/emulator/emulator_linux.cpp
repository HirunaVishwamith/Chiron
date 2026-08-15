/**
 * Instantiates the emulator on emulator.h and also
 * provides a chronological order of how the functions
 * in emulator.h should be run
*/

#include "emulator.h"
#include <csignal>
#include <cstdint>
#include <vector>

#include "sim/harness/common/completion.h"

emulator emu;

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum) {
  disable_raw_mode();
  tcflush(0, TCIFLUSH); 
  // Terminate program
  exit(signum);
}

int main(int argc, char **argv) {
  // Image path comes from argv[1] (the makefile passes the benchmark .bin
  // or a Linux image). Flags may follow. Fall back to the default runtime
  // image when run bare.
  const char *image = (argc > 1 && argv[1][0] != '-') ? argv[1] : "sim/data/Image";
  const harness::Completion done = harness::Completion::parse(argc, argv);

  // initiate registers and memory
  emu.init(image);

  // Restore the cooked terminal on Ctrl-C so the shell isn't left in raw mode.
  signal(SIGINT, signal_callback_handler);

  enable_raw_mode();
  unsigned long instret0 = 0, sw_count = 0;
  while (1) {
    // Simple balanced round-robin + deliver. No PC-range suppression of
    // interrupts (the key fix). Small per-hart unroll only when a hart is
    // actively in the stop region to help serialized stop_machine finish.
    emu.tick_only();
    for (int i = 0; i < NUM_HARTS; i++) {
      emu.step_hart_only(i);
      uint64_t pc = emu.get_pc(i);
      if (done.active()) {
        uint64_t a0 = emu.reg_file(i)[10];
        if (done.hit(pc, a0)) {
          disable_raw_mode();
          return 0;
        }
      }
      if (pc >= 0x802aa000ULL && pc < 0x802ab000ULL) {
        for (int k = 0; k < 1023; k++) {
          emu.step_hart_only(i);
          pc = emu.get_pc(i);
          if (pc < 0x802aa000ULL || pc >= 0x802ab000ULL) break;
        }
      }
    }
    emu.deliver_interrupts();
  }
  disable_raw_mode();
  return 0;
}