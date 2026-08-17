// linux_sim.cpp — boot a chiron Linux image on the RTL (Verilated) core alone.
//
// Unlike lockstep_linux.cpp this runs *only* the RTL: no golden model, no
// per-instruction run.log, no register comparison. That debug machinery is what
// makes lock-step crawl; dropping it lets the core run as fast as Verilator
// allows (~40K cycles/sec on a 4-threaded model) so you can watch Linux boot on
// the actual RTL and type at its console.
//
//   usage: linux_sim.out <image.bin> <dtb> <bootrom> [options]
//
// Built twice from this file:
//   linux_sim.out       --savable model (make linux-sim); checkpoints work
//   linux_sim_fast.out  no --savable     (make linux-sim-fast); same boot, faster
//
//     --debug              write a harness log and take periodic checkpoints
//     --log <path>         where the debug log goes   (default build/linux-sim.log)
//     --ckpt-dir <dir>     where checkpoints go       (default ckpt)
//     --ckpt-every <n>     cycles between checkpoints (default 20,000,000)
//     --ckpt-keep <n>      checkpoints to retain, 0 = all (default 8)
//     --no-ckpt            --debug logging, but no checkpoints
//     --hb <n>             cycles between log heartbeats (default 100,000)
//     --restore <file>     resume from a checkpoint instead of booting
//                          (not available in linux_sim_fast.out)
//
// STDOUT IS THE GUEST'S CONSOLE AND NOTHING ELSE. Every byte on it was
// transmitted by the running kernel, so `make linux-sim | tee boot.log` yields a
// boot log that is purely the boot. Harness chatter — image loading, progress,
// checkpoint bookkeeping — goes to the --debug log file or nowhere at all.
//
// This is SLOW in absolute terms: ~40K cycles/sec, and a full boot to the login
// prompt is ~3 billion cycles, so budget the better part of a day. bbl must also
// memcpy a multi-MB kernel before the first UART byte appears, so several
// minutes of silence at the start is normal, not a hang. If you want to watch
// progress, run with --debug and tail the log.
//
// Input: the host's stdin is forwarded to uart0's RX, so this is a real
// interactive console — at "buildroot login: " type root, then nproc.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <fcntl.h>
#include <unistd.h>

#include "sim/rtl/rtl_model.h"
#ifndef CHIRON_NO_SAVE
#include "verilated_save.h"
#endif
#include "sim/harness/common/args.h"
#include "sim/harness/common/simlog.h"

#ifndef CHIRON_NO_SAVE
// Bumped whenever the harness state written alongside the model changes, so a
// stale checkpoint is rejected instead of silently restoring garbage into a
// 256 MB DRAM array.
static const uint64_t kCkptMagic = 0x434849524F4E4C58ULL;  // "CHIRONLX"
static const uint64_t kCkptVersion = 1;
#endif

int main(int argc, char **argv) {
  // Positional args stay first for backward compatibility with every script
  // and Makefile target that already calls this harness.
  const char *image   = (argc > 1 && argv[1][0] != '-') ? argv[1] : "sim/data/Image";
  const char *dtb     = (argc > 2 && argv[2][0] != '-') ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3 && argv[3][0] != '-') ? argv[3] : "sim/data/boot.bin";

  const bool debug      = harness::has_flag(argc, argv, "--debug");
  const char *log_path  = harness::find_arg(argc, argv, "--log", "build/linux-sim.log");
  const char *ckpt_dir  = harness::find_arg(argc, argv, "--ckpt-dir", "ckpt");
  const char *restore   = harness::find_arg(argc, argv, "--restore", nullptr);
  const bool no_ckpt    = harness::has_flag(argc, argv, "--no-ckpt");
  const uint64_t ckpt_every =
      std::strtoull(harness::find_arg(argc, argv, "--ckpt-every", "20000000"), nullptr, 0);
  const uint64_t ckpt_keep =
      std::strtoull(harness::find_arg(argc, argv, "--ckpt-keep", "8"), nullptr, 0);
  const uint64_t hb_every =
      std::strtoull(harness::find_arg(argc, argv, "--hb", "100000"), nullptr, 0);

  if (debug) simlog::open(log_path);
#ifdef CHIRON_NO_SAVE
  // Fast binary: same console boot, no Verilator --savable surface. Checkpoints
  // live on linux_sim.out (make linux-sim / make sim-ckpt).
  if (restore) {
    std::fprintf(stderr,
      "[linux_sim] this binary has no checkpoints (make linux-sim-fast). "
      "Use `make linux-sim RESTORE=%s` on the savable model.\n", restore);
    return 1;
  }
  const bool ckpt_on = false;
  (void)no_ckpt; (void)ckpt_every; (void)ckpt_keep; (void)ckpt_dir;
#else
  // Checkpointing is a debugging aid and rides on --debug: a normal boot should
  // not quietly start writing 256 MB files into the working tree.
  const bool ckpt_on = debug && !no_ckpt && ckpt_every > 0;
#endif

  simulator bench;
  uint64_t steps = 0;

#ifdef CHIRON_NO_SAVE
  bench.init(image, dtb, bootrom);
  SIMLOG("[boot] image=%s dtb=%s bootrom=%s\n", image, dtb, bootrom);
#else
  if (restore) {
    // Restore overwrites every register and all of DRAM, so the model must not
    // be reset or loaded first — see rtl_model.h::init_no_image().
    bench.init_no_image();
    VerilatedRestore rs;
    rs.open(restore);
    uint64_t magic = 0, version = 0, cyc = 0;
    rs >> magic; rs >> version; rs >> cyc; rs >> steps;
    if (magic != kCkptMagic || version != kCkptVersion) {
      std::fprintf(stderr,
        "[linux_sim] '%s' is not a linux_sim checkpoint (or was written by a "
        "different version) — refusing to restore\n", restore);
      return 1;
    }
    rs >> *bench.raw();
    rs.close();
    bench.tickcount = (unsigned long)cyc;
    SIMLOG("[restore] resumed from %s at cycle %llu (steps=%llu)\n",
           restore, (unsigned long long)cyc, (unsigned long long)steps);
  } else {
    bench.init(image, dtb, bootrom);
    SIMLOG("[boot] image=%s dtb=%s bootrom=%s\n", image, dtb, bootrom);
  }
#endif

  if (ckpt_on) {
    char cmd[1100];
    std::snprintf(cmd, sizeof cmd, "mkdir -p '%s' 2>/dev/null", ckpt_dir);
    if (system(cmd) != 0) { /* reported below via isOpen() */ }
    SIMLOG("[ckpt] every %llu cycles into %s/ (~256 MB each, keeping %llu)\n",
           (unsigned long long)ckpt_every, ckpt_dir,
           (unsigned long long)ckpt_keep);
  }

  // ── Console input ──────────────────────────────────────────────────────────
  // stdin is non-blocking so a cycle is never spent waiting on a keystroke, and
  // it is polled every kInputPoll cycles rather than every cycle because a
  // read() syscall per simulated cycle would dominate the run time.
  Vsystem *tb = bench.raw();
  tb->hostInput_valid = 0;
  tb->hostInput_char  = 0;
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
  const uint64_t kInputPoll = 4096;
  uint64_t input_tick = 0;

  using clock = std::chrono::steady_clock;
  auto t_start = clock::now();
  auto t_last  = t_start;
  uint64_t last_steps = steps;
  uint64_t last_pc[4] = {};
  // Per-hart PC band over the current heartbeat window (min/max of every
  // committed PC sampled). A livelock spins across a *changing* PC each commit,
  // so a single snapshot cannot see it — but a tight loop keeps [min,max] small
  // and, crucially, identical window after window. Tracking that band lets the
  // heartbeat flag "pinned" harts, the true livelock signature, and print the
  // exact address range to disassemble.
  uint64_t band_lo[4], band_hi[4];
  uint64_t prev_lo[4] = {}, prev_hi[4] = {};
  for (int i = 0; i < 4; ++i) { band_lo[i] = ~0ULL; band_hi[i] = 0; }

  uint64_t next_hb   = bench.tickcount + hb_every;
#ifndef CHIRON_NO_SAVE
  uint64_t next_ckpt = ckpt_on
      ? ((bench.tickcount / ckpt_every) + 1) * ckpt_every : ~0ULL;
  std::deque<std::string> ckpts;
#endif

  // UART TX from all four cores' uartPorts is streamed to stdout from inside
  // step_any_nodump() (the SHOW_TERMINAL hook in rtl_model.h). Progress is
  // judged on ANY core committing, not just core 0 — under SMP core 0 may idle
  // while another hart runs.
  while (true) {
    if (bench.step_any_nodump() == 1) {  // no core committed for STEP_TIMEOUT
      // A global wedge is a failure, not chatter: it goes to stderr even
      // without --debug, so a run that dies overnight says so.
      std::fprintf(stderr,
        "\n[linux_sim] ALL cores stalled: no commit on any hart for "
        "STEP_TIMEOUT cycles (cycle %lu)\n"
        "            pc0=0x%08lx pc1=0x%08lx pc2=0x%08lx pc3=0x%08lx\n",
        bench.tickcount,
        (unsigned long)bench.core_pc(0), (unsigned long)bench.core_pc(1),
        (unsigned long)bench.core_pc(2), (unsigned long)bench.core_pc(3));
      SIMLOG("[wedge] no commit on any hart at cycle %lu\n", bench.tickcount);
      simlog::close();
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

    // Everything below is debug bookkeeping; skip it entirely on a normal run
    // so the fast path stays fast.
    if (!debug) continue;

    // Fold every committed PC into the per-hart band for this window.
    for (int i = 0; i < 4; ++i) {
      uint64_t p = bench.core_pc(i);
      if (p < band_lo[i]) band_lo[i] = p;
      if (p > band_hi[i]) band_hi[i] = p;
    }

    if (bench.tickcount >= next_hb) {
      next_hb = bench.tickcount + hb_every;
      auto now = clock::now();
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
      SIMLOG("+%6.0fs steps=%-11llu (%6.0f/s) cyc=%-12lu  "
             "pc0=%08lx%c pc1=%08lx%c pc2=%08lx%c pc3=%08lx%c%s\n",
             total, (unsigned long long)steps,
             (steps - last_steps) / (dt > 0 ? dt : 1), bench.tickcount,
             (unsigned long)pc[0], flag[0], (unsigned long)pc[1], flag[1],
             (unsigned long)pc[2], flag[2], (unsigned long)pc[3], flag[3],
             pinned == 4 ? "  <<< ALL 4 PINNED (livelock?)" : "");
      if (pinned == 4)
        SIMLOG("        bands: c0=[%08lx..%08lx] c1=[%08lx..%08lx] "
               "c2=[%08lx..%08lx] c3=[%08lx..%08lx]\n",
               (unsigned long)band_lo[0], (unsigned long)band_hi[0],
               (unsigned long)band_lo[1], (unsigned long)band_hi[1],
               (unsigned long)band_lo[2], (unsigned long)band_hi[2],
               (unsigned long)band_lo[3], (unsigned long)band_hi[3]);
      t_last = now;
      last_steps = steps;
      for (int i = 0; i < 4; ++i) {
        last_pc[i] = pc[i];
        prev_lo[i] = band_lo[i]; prev_hi[i] = band_hi[i];
        band_lo[i] = ~0ULL; band_hi[i] = 0;  // reset band for next window
      }
    }

#ifndef CHIRON_NO_SAVE
    if (bench.tickcount >= next_ckpt) {
      next_ckpt = bench.tickcount + ckpt_every;
      char path[1024];
      std::snprintf(path, sizeof path, "%s/ckpt_%012lu.bin", ckpt_dir,
                    bench.tickcount);
      VerilatedSave sv;
      sv.open(path);
      // VerilatedSave::open() does not fail loudly — on error it just sets
      // m_isOpen=false and every subsequent write is a no-op. Without this
      // check a run reports nothing wrong while silently producing no
      // checkpoints, which you only discover when you go to restore.
      if (!sv.isOpen()) {
        SIMLOG("[ckpt] ERROR: cannot open %s — checkpointing DISABLED for this "
               "run; the boot continues\n", path);
        next_ckpt = ~0ULL;
      } else {
        // Verilator's operator<< takes a non-const reference, so the header
        // fields need writable locals — a `const` constant will not bind.
        uint64_t magic = kCkptMagic, version = kCkptVersion;
        uint64_t cyc = bench.tickcount, saved_steps = steps;
        sv << magic; sv << version; sv << cyc; sv << saved_steps;
        sv << *tb;
        sv.close();
        SIMLOG("[ckpt] wrote %s\n", path);
        ckpts.push_back(path);
        while (ckpt_keep && ckpts.size() > ckpt_keep) {
          std::remove(ckpts.front().c_str());
          SIMLOG("[ckpt] pruned %s\n", ckpts.front().c_str());
          ckpts.pop_front();
        }
      }
    }
#endif
  }
  simlog::close();
  return 0;
}
