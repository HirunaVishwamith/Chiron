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

  // Progress heartbeat on stderr — ON by default (set LINUX_SIM_HB=0 to
  // silence). Prints steps + all four cores' PCs every ~5 s: under SMP any
  // hart can be the one making progress, so a single-core PC is not enough to
  // tell a slow boot from a wedge. A hart whose PC hasn't moved since the last
  // beat is flagged with '*' (spin-wait or wfi — normal for lottery losers,
  // suspicious for all four at once).
  const char *hb_env = std::getenv("LINUX_SIM_HB");
  const bool heartbeat = !(hb_env && hb_env[0] == '0');
  using clock = std::chrono::steady_clock;
  auto t_start = clock::now();
  auto t_last  = t_start;
  uint64_t steps = 0, last_steps = 0;
  uint64_t last_pc[4] = {};

  // Run forever; UART TX from all four cores' uartPorts is streamed to stdout
  // from inside step_any_nodump() (the SHOW_TERMINAL hook in rtl_model.h).
  // Progress/stall is judged on ANY core committing, not just core 0 — under
  // the SMP image core 0 may idle while another hart runs.
  while (true) {
    if (bench.step_any_nodump() == 1) {  // no core committed for STEP_TIMEOUT
      std::fprintf(stderr,
        "\n[linux_sim] ALL cores stalled: no commit on any hart for "
        "STEP_TIMEOUT cycles (cycle %lu)\n"
        "            pc0=0x%08lx pc1=0x%08lx pc2=0x%08lx pc3=0x%08lx\n",
        bench.tickcount,
        (unsigned long)bench.core_pc(0), (unsigned long)bench.core_pc(1),
        (unsigned long)bench.core_pc(2), (unsigned long)bench.core_pc(3));
      return 1;
    }
    ++steps;

    if (heartbeat) {
      auto now = clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last).count() >= 5000) {
        double dt = std::chrono::duration<double>(now - t_last).count();
        double total = std::chrono::duration<double>(now - t_start).count();
        uint64_t pc[4];
        for (int i = 0; i < 4; ++i) pc[i] = bench.core_pc(i);
        std::fprintf(stderr,
          "[linux_sim] +%5.0fs  steps=%-10lu (%6.0f/s)  cycles=%-12lu  "
          "pc0=0x%08lx%s pc1=0x%08lx%s pc2=0x%08lx%s pc3=0x%08lx%s\n",
          total, (unsigned long)steps,
          (steps - last_steps) / (dt > 0 ? dt : 1), bench.tickcount,
          (unsigned long)pc[0], pc[0] == last_pc[0] ? "*" : " ",
          (unsigned long)pc[1], pc[1] == last_pc[1] ? "*" : " ",
          (unsigned long)pc[2], pc[2] == last_pc[2] ? "*" : " ",
          (unsigned long)pc[3], pc[3] == last_pc[3] ? "*" : " ");
        std::fflush(stderr);
        t_last = now;
        last_steps = steps;
        for (int i = 0; i < 4; ++i) last_pc[i] = pc[i];
      }
    }
  }
  return 0;
}
