/**
 * Run a rv-test given as the first command, return non-zero
 * value if test fails
 */

#include "emulator.h"

emulator emu;

#define TICK_LIMIT 10000 // How many instructions to execute

int main(int argc, char* argv[]) {

	for (int32_t test_no = 1; test_no < argc; test_no++) {
		// initiate memory
		emu.init(argv[test_no]);
		printf("Running riscv-test image: %-50s \t result: ", argv[test_no]);
		for (uint32_t i = 0; i < TICK_LIMIT; i++) {
			// steps through one architectural change of pc
			emu.step();

			// sets up interrupts to move to exception handler
			// in next step()
			emu.set_interrupts();

			uint64_t ret = emu.memory_read(0x10001000);
			switch (ret) {
			case 0: // test not finished yet
				break;
			case 1: // test passed
				printf("passed \n");
				goto test_end;

			default: // test failed
				printf("failed at test: %03ld \n", ret);
				return 1;
				break;
			};
		};
		// timeout
		printf("failed due to timed out \n");
		return 2;
test_end:
		;
	};
	return 0;
}
