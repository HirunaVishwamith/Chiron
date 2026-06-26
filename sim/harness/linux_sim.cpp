// linux_sim.cpp — boot a chiron Linux image on the RTL (Verilated) core alone.
//
// Unlike lockstep_linux.cpp this runs *only* the RTL: no golden model, no
// per-instruction run.log, no register comparison. That debug machinery is
// what makes lock-step crawl; dropping it lets the core run as fast as
// Verilator allows (~thousands of cycles/sec) so you can watch Linux boot on
// the actual RTL and see its UART console output live.
//
// Build with -DSHOW_TERMINAL so rtl_model.h streams the core's UART TX bytes
// (core0OutChar_*) to stdout as they are written.
//
//   usage: linux_sim.out <image.bin> <dtb> <bootrom>
//
// IMPORTANT — this is SLOW. The core runs at ~thousands of cycles/sec, and bbl
// must memcpy the whole multi-MB kernel payload before the kernel prints its
// first UART byte. Expect *no console output for many minutes*; that is normal,
// not a hang. The periodic [linux_sim] heartbeat on stderr shows the committed
// instruction count + current PC so you can see forward progress (PC advancing)
// versus a real deadlock (PC frozen while cycles climb).
//
// Note on input: the RTL's UART model (quard_uart.scala) has no RX input port
// — reads of the uartlite RX/STATUS registers return constants — so keystrokes
// cannot reach the guest here. For an interactive shell use `make linux-emu`
// (the golden model), which boots in seconds and reads stdin.
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "sim/rtl/rtl_model.h"

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "sim/data/Image";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);

  std::printf("\n***** booting Linux on the RTL core (Ctrl-C to stop) *****\n");
  std::printf("      (RTL is slow: expect no console output for several minutes\n");
  std::printf("       while bbl copies the kernel into place)\n\n");
  std::fflush(stdout);

  // Optional progress heartbeat on stderr — enable with LINUX_SIM_HB=1. Off by
  // default so it doesn't interleave with the guest console; when on it prints
  // committed instrs + PC every ~3 s so a real deadlock (PC frozen while cycles
  // climb) is visible versus a merely slow boot.
  const bool heartbeat = std::getenv("LINUX_SIM_HB") != nullptr;
  using clock = std::chrono::steady_clock;
  auto t_start = clock::now();
  auto t_last  = t_start;
  uint64_t commits = 0, last_commits = 0, last_pc = 0;

  // Run forever; UART TX is streamed to stdout from inside step_nodump()
  // (the SHOW_TERMINAL hook in rtl_model.h). step_nodump() keeps no VCD and
  // writes no trace, so the only console output is the guest itself.
  while (true) {
    if (bench.step_nodump() == 1) {  // run_until_commit hit STEP_TIMEOUT
      std::fprintf(stderr,
        "\n[linux_sim] core stalled: no commit for STEP_TIMEOUT cycles "
        "at pc=0x%lx (cycle %lu)\n",
        (unsigned long)bench.prev_pc, bench.tickcount);
      return 1;
    }
    ++commits;

    if (heartbeat) {
      auto now = clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last).count() >= 3000) {
        double dt = std::chrono::duration<double>(now - t_last).count();
        double total = std::chrono::duration<double>(now - t_start).count();
        uint64_t pc = bench.prev_pc;
        std::fprintf(stderr,
          "[linux_sim] +%5.0fs  instrs=%-10lu (%6.0f/s)  cycles=%-12lu  pc=0x%08lx%s\n",
          total, (unsigned long)commits,
          (commits - last_commits) / (dt > 0 ? dt : 1),
          bench.tickcount, (unsigned long)pc,
          (pc == last_pc) ? "  <-- PC not advancing (possible deadlock)" : "");
        std::fflush(stderr);
        t_last = now;
        last_commits = commits;
        last_pc = pc;
      }
    }
  }
  return 0;
}
