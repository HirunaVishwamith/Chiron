// linux_sim_trace.cpp — boot a chiron Linux image on the RTL, watching for the
// known SMP-boot wedge (harts spinning forever inside the timekeeping seqlock,
// ktime_get / ktime_get_update_offsets_now — see chiron's SMP bring-up notes)
// and capturing a bounded VCD waveform right around the point it sets in.
//
// Rationale: a full-run VCD (500M+ cycles) is both far too slow to dump (VCD
// writes are the dominant per-cycle cost) and far too large to store or
// inspect. Instead this runs UNDUMPED at full RTL speed — same as
// linux_sim.cpp — until a hart's committed PC has sat continuously inside one
// of the watched function ranges for WEDGE_THRESHOLD cycles (way beyond the
// few-hundred-cycle cost of an ordinary call into ktime_get during normal
// boot), then flips on VCD dumping for a further bounded window and exits.
//
// Must be linked against the TRACE-capable model (obj_dir, not obj_dir_fast):
// dumping is unavailable on the fast no-trace build.
//
//   usage: linux_sim_trace.out <image.bin> <dtb> <bootrom> [vcd_out]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "sim/rtl/rtl_model.h"

namespace {

// [start, end) function ranges to watch, extracted via nm from the exact
// vmlinux that produced bins/linux-q4.bin (mc-linux/linux/vmlinux,
// 2026-07-02 build) — update if the kernel image is ever rebuilt.
struct Watch { uint64_t lo, hi; const char *name; };
constexpr Watch kWatched[] = {
  {0x802820d0, 0x8028220c, "ktime_get"},
  {0x80283870, 0x80283a10, "ktime_get_update_offsets_now"},
};

// Cycles a hart's committed PC must sit continuously inside a watched range
// before we call it a wedge, not an ordinary transient call (~8000 cyc/s sim
// speed, so 2M cycles is ~250s of confirmation — a normal call returns in far
// fewer than 1000 cycles).
constexpr uint64_t kWedgeThresholdCycles = 2'000'000ULL;

// How many further cycles to keep dumping once triggered, before stopping.
constexpr uint64_t kTraceWindowCycles = 2'000'000ULL;

const char *watched_name(uint64_t pc) {
  for (const auto &w : kWatched)
    if (pc >= w.lo && pc < w.hi) return w.name;
  return nullptr;
}

}  // namespace

int main(int argc, char **argv) {
  const char *image     = (argc > 1) ? argv[1] : "sim/data/Image";
  const char *dtb        = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom    = (argc > 3) ? argv[3] : "sim/data/boot.bin";
  const char *trace_file = (argc > 4) ? argv[4] : "build/linux_wedge_trace.vcd";

  simulator bench;
  bench.init(image, dtb, bootrom, /*enable_trace=*/true, trace_file);

  std::printf("\n***** booting Linux on the RTL core, watching for the "
              "timekeeping-seqlock wedge *****\n");
  std::printf("      undumped until triggered; VCD written to %s\n\n", trace_file);
  std::fflush(stdout);

  using clock = std::chrono::steady_clock;
  auto t_start = clock::now();
  auto t_last  = t_start;

  uint64_t streak_cycles[4]  = {};
  uint64_t last_seen_pc[4]   = {};
  uint64_t last_tickcount    = 0;

  bool     triggered         = false;
  uint64_t trigger_tickcount = 0;

  while (true) {
    uint64_t before = bench.tickcount;
    int rc = bench.step_any(/*dump=*/triggered);
    uint64_t elapsed = bench.tickcount - before;

    if (rc == 1) {
      std::fprintf(stderr,
        "\n[linux_sim_trace] ALL cores stalled: no commit for STEP_TIMEOUT "
        "cycles (cycle %lu)\n", bench.tickcount);
      break;
    }

    for (int i = 0; i < 4; ++i) {
      uint64_t pc = bench.core_pc(i);
      const char *w = watched_name(pc);
      if (w) {
        streak_cycles[i] += elapsed;
      } else {
        streak_cycles[i] = 0;
      }
      last_seen_pc[i] = pc;
    }

    if (!triggered) {
      for (int i = 0; i < 4; ++i) {
        if (streak_cycles[i] >= kWedgeThresholdCycles) {
          triggered = true;
          trigger_tickcount = bench.tickcount;
          std::fprintf(stderr,
            "\n[linux_sim_trace] WEDGE DETECTED: hart%d parked in %s for "
            ">=%lu cycles (cycle %lu) -- starting VCD capture for %lu more "
            "cycles\n",
            i, watched_name(last_seen_pc[i]), streak_cycles[i],
            bench.tickcount, kTraceWindowCycles);
          std::fprintf(stderr,
            "            pc0=0x%08lx pc1=0x%08lx pc2=0x%08lx pc3=0x%08lx\n",
            (unsigned long)last_seen_pc[0], (unsigned long)last_seen_pc[1],
            (unsigned long)last_seen_pc[2], (unsigned long)last_seen_pc[3]);
          break;
        }
      }
    } else if (bench.tickcount - trigger_tickcount >= kTraceWindowCycles) {
      std::fprintf(stderr,
        "\n[linux_sim_trace] capture window complete at cycle %lu "
        "(triggered at %lu); VCD written to %s\n",
        bench.tickcount, trigger_tickcount, trace_file);
      break;
    }

    auto now = clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last).count() >= 5000) {
      double total = std::chrono::duration<double>(now - t_start).count();
      std::fprintf(stderr,
        "[linux_sim_trace] +%5.0fs  cycles=%-12lu  %s  "
        "pc0=0x%08lx pc1=0x%08lx pc2=0x%08lx pc3=0x%08lx  "
        "streak=[%lu,%lu,%lu,%lu]\n",
        total, bench.tickcount, triggered ? "DUMPING" : "watching",
        (unsigned long)last_seen_pc[0], (unsigned long)last_seen_pc[1],
        (unsigned long)last_seen_pc[2], (unsigned long)last_seen_pc[3],
        streak_cycles[0], streak_cycles[1], streak_cycles[2], streak_cycles[3]);
      std::fflush(stderr);
      t_last = now;
    }
  }

  return 0;
}
