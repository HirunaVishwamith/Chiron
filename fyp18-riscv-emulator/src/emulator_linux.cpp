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

int main(int argc, char* argv[]) {
	// initaite registers and memory
	emu.init(argv[1]);

	enable_raw_mode();
	while (1) {
		// steps through one architectural change of pc
		emu.step();

		// sets up interrupts to move to exception handler
		// in next step()
		emu.set_interrupts();
	}
	disable_raw_mode();
	return 0;
}