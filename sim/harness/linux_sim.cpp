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
#include <fcntl.h>
#include <unistd.h>

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
  // Per-hart PC band over the current heartbeat window (min/max of every
  // committed PC sampled). A livelock spins across a *changing* PC each commit,
  // so a single snapshot can't see it — but a tight loop keeps [min,max] small
  // and, crucially, *identical* window after window. We track that band and the
  // previous window's band so the heartbeat can flag "pinned" harts (band tiny
  // AND unchanged) — the true livelock signature — and print the exact address
  // range to disassemble.
  uint64_t band_lo[4], band_hi[4];
  uint64_t prev_lo[4] = {}, prev_hi[4] = {};
  for (int i = 0; i < 4; ++i) { band_lo[i] = ~0ULL; band_hi[i] = 0; }

  // ── Console input ──────────────────────────────────────────────────────────
  // Forward the host's stdin to uart0's RX so this is a real interactive
  // console: at "buildroot login: " you can type root, then nproc. Before this,
  // system.scala tied hostInput off and the only input was uartPort's
  // compiled-in ROM, which spoke a register map the Linux uartlite driver never
  // reads — so the console was output-only and the boot parked at the prompt.
  //
  // stdin is put in non-blocking mode so a cycle is never spent waiting on a
  // keystroke, and it is polled every kInputPoll cycles rather than every cycle
  // because a read() syscall per simulated cycle would dominate the run time.
  // The ROM still auto-logs-in when nobody types, so redirected/idle stdin
  // behaves exactly as before.
  Vsystem *tb = bench.raw();
  tb->hostInput_valid = 0;
  tb->hostInput_char  = 0;
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
  const uint64_t kInputPoll = 4096;
  uint64_t input_tick = 0;

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

    // Retire a delivered character, then offer the next one.
    //
    // This loop advances commit-to-commit, and a single step_any_nodump() can
    // span many cycles, so it CANNOT reliably observe a one-cycle signal.
    // hostInputConsumed is therefore held by MultiUart's hostTaken latch until
    // this side drops hostInput_valid (a four-phase handshake) — that is what
    // makes sampling here safe. Do not "simplify" the RTL back to a pulse:
    // uartrx_test showed the console then wedges after two or three keystrokes,
    // which is a failure you only discover 14 h into a boot, at the login
    // prompt, with no way to type.
    if (tb->hostInputConsumed) tb->hostInput_valid = 0;
    // The !hostInputConsumed term completes phase 3: valid must be low long
    // enough for hostTaken to clear before the next byte is presented, or the
    // handshake never re-arms. The kInputPoll gap makes that true in practice
    // anyway; this makes it true by construction.
    if (!tb->hostInput_valid && !tb->hostInputConsumed &&
        ++input_tick >= kInputPoll) {
      input_tick = 0;
      unsigned char ch;
      if (::read(STDIN_FILENO, &ch, 1) == 1) {
        tb->hostInput_char  = ch;
        tb->hostInput_valid = 1;
      }
    }

    // Fold every committed PC into the per-hart band for this window.
    for (int i = 0; i < 4; ++i) {
      uint64_t p = bench.core_pc(i);
      if (p < band_lo[i]) band_lo[i] = p;
      if (p > band_hi[i]) band_hi[i] = p;
    }

    if (heartbeat) {
      auto now = clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last).count() >= 5000) {
        double dt = std::chrono::duration<double>(now - t_last).count();
        double total = std::chrono::duration<double>(now - t_start).count();
        uint64_t pc[4];
        for (int i = 0; i < 4; ++i) pc[i] = bench.core_pc(i);
        // A hart is "pinned" if its band this window spans < 4 KB AND is
        // identical to last window's band — spinning in a fixed code region.
        int pinned = 0;
        char flag[4];
        for (int i = 0; i < 4; ++i) {
          bool tight = (band_hi[i] >= band_lo[i]) &&
                       (band_hi[i] - band_lo[i] < 0x1000);
          bool same  = (band_lo[i] == prev_lo[i]) && (band_hi[i] == prev_hi[i]);
          flag[i] = (tight && same) ? '#' : (pc[i] == last_pc[i] ? '*' : ' ');
          if (tight && same) ++pinned;
        }
        std::fprintf(stderr,
          "[linux_sim] +%6.0fs steps=%-11lu (%6.0f/s) cyc=%-12lu  "
          "pc0=%08lx%c pc1=%08lx%c pc2=%08lx%c pc3=%08lx%c%s\n",
          total, (unsigned long)steps,
          (steps - last_steps) / (dt > 0 ? dt : 1), bench.tickcount,
          (unsigned long)pc[0], flag[0], (unsigned long)pc[1], flag[1],
          (unsigned long)pc[2], flag[2], (unsigned long)pc[3], flag[3],
          pinned == 4 ? "  <<< ALL 4 PINNED (livelock?)" : "");
        // On an all-pinned window, dump each hart's spin band once.
        if (pinned == 4)
          std::fprintf(stderr,
            "            bands: c0=[%08lx..%08lx] c1=[%08lx..%08lx] "
            "c2=[%08lx..%08lx] c3=[%08lx..%08lx]\n",
            (unsigned long)band_lo[0], (unsigned long)band_hi[0],
            (unsigned long)band_lo[1], (unsigned long)band_hi[1],
            (unsigned long)band_lo[2], (unsigned long)band_hi[2],
            (unsigned long)band_lo[3], (unsigned long)band_hi[3]);
        std::fflush(stderr);
        t_last = now;
        last_steps = steps;
        for (int i = 0; i < 4; ++i) {
          last_pc[i] = pc[i];
          prev_lo[i] = band_lo[i]; prev_hi[i] = band_hi[i];
          band_lo[i] = ~0ULL; band_hi[i] = 0;  // reset band for next window
        }
      }
    }
  }
  return 0;
}
