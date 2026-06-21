/**
 * Instantiates the emulator on emulator.h and also
 * provides a chronological order of how the functions
 * in emulator.h should be run
*/

#include "emulator.h"

emulator emu;

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum) {
  disable_raw_mode();
  tcflush(0, TCIFLUSH); 
  // Terminate program
  exit(signum);
}

int main(int argc, char **argv) {
  // Image path comes from argv[1] (the makefile passes the benchmark .bin);
  // fall back to the default runtime image when run bare.
  const char *image = (argc > 1) ? argv[1] : "sim/data/Image";

  // initiate registers and memory
  emu.init(image);

  enable_raw_mode();
  while (1) {
    // steps through one architectural change of pc
    emu.step();
    //emu.show_registers();
    // sets up interrupts to move to exception handler
    // in next step()
    emu.set_interrupts();
  }
  disable_raw_mode();
  return 0;
}