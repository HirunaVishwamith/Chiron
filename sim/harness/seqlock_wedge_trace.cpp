// seqlock_wedge_trace.cpp — run the mt-seqlock microbenchmark on the RTL,
// watching every hart's committed PC for the kind of dead stop we saw under
// profile_quad (a core's PC identical across ten consecutive 500,000-cycle
// samples -- ~4.5M cycles parked on one instruction with no forward
// progress). Unlike linux_sim_trace.cpp (which watches for a hart parked
// inside a known kernel function), this benchmark is small enough that we
// don't need to name a suspect range: ANY hart whose PC hasn't changed for
// kWedgeThresholdCycles is by definition wedged (this code has no legitimate
// reason to sit on one instruction that long).
//
// On trigger:
//   - flips on VCD dumping (bounded to kTraceWindowCycles further cycles)
//   - writes a full per-cycle text trace (PC + commit flag for all 4 cores,
//     plus a full 33-register dump for all 4 cores every kRegDumpStride
//     cycles) to the reg-log file, covering the same window
//
// Must be linked against the TRACE-capable model (obj_dir, not obj_dir_fast).
//
//   usage: seqlock_wedge_trace.out [--image <bin>] [--threshold <cycles>]
//                                  [--window <cycles>] [--max-cycles <n>]
//                                  [--vcd <path>] [--reglog <path>]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>

#include "verilated.h"
#include "verilated_vcd_c.h"
#include "Vsystem.h"

#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"

using namespace harness;

namespace {

// All 32 GPRs; mstatus is bound separately at index 32.
#define FOR_EACH_GPR(X)                                                \
  X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)                       \
  X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15)                     \
  X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23)                     \
  X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)

uint64_t *g_regs[4][33] = {};

void bind_registers(Vsystem *tb) {
#define BIND0(i) g_regs[0][i] = &tb->registersOut0_##i;
#define BIND1(i) g_regs[1][i] = &tb->registersOut1_##i;
#define BIND2(i) g_regs[2][i] = &tb->registersOut2_##i;
#define BIND3(i) g_regs[3][i] = &tb->registersOut3_##i;
  FOR_EACH_GPR(BIND0) g_regs[0][32] = &tb->registersOut0_32;
  FOR_EACH_GPR(BIND1) g_regs[1][32] = &tb->registersOut1_32;
  FOR_EACH_GPR(BIND2) g_regs[2][32] = &tb->registersOut2_32;
  FOR_EACH_GPR(BIND3) g_regs[3][32] = &tb->registersOut3_32;
#undef BIND0
#undef BIND1
#undef BIND2
#undef BIND3
}

uint64_t core_pc(Vsystem *tb, int c) {
  switch (c) {
    case 0: return tb->robOut0_pc;
    case 1: return tb->robOut1_pc;
    case 2: return tb->robOut2_pc;
    default: return tb->robOut3_pc;
  }
}

bool core_commit(Vsystem *tb, int c) {
  switch (c) {
    case 0: return tb->robOut0_commitFired;
    case 1: return tb->robOut1_commitFired;
    case 2: return tb->robOut2_commitFired;
    default: return tb->robOut3_commitFired;
  }
}

// One clock, VCD-dumped at timeline position t.
void tick_dump(Vsystem *tb, VerilatedVcdC *tfp, unsigned t) {
  tb->eval();
  tfp->dump((vluint64_t)t * 10 - 2);
  tb->clock = 1; tb->eval();
  tfp->dump((vluint64_t)t * 10);
  tb->clock = 0; tb->eval();
  tfp->dump((vluint64_t)t * 10 + 5);
  tfp->flush();
}

void write_regs(FILE *f, uint64_t cycle) {
  std::fprintf(f, "--- cycle %llu: full register dump ---\n",
               (unsigned long long)cycle);
  for (int c = 0; c < 4; ++c) {
    std::fprintf(f, "core%d:\n", c);
    for (int i = 0; i < 32; ++i) {
      std::fprintf(f, "  x%-2d: %016lx%s", i, (unsigned long)*g_regs[c][i],
                   (i % 4 == 3) ? "\n" : "");
    }
    std::fprintf(f, "  mstatus: %016lx\n", (unsigned long)*g_regs[c][32]);
  }
  std::fflush(f);
}

}  // namespace

int main(int argc, char **argv) {
  const char *image   = find_arg(argc, argv, "--image", "bins/mt-seqlock-q4.bin");
  const char *vcd_path = find_arg(argc, argv, "--vcd", "build/seqlock_wedge.vcd");
  const char *reglog_path = find_arg(argc, argv, "--reglog", "build/seqlock_wedge_regs.log");
  uint64_t threshold  = std::strtoull(find_arg(argc, argv, "--threshold", "20000"), nullptr, 10);
  uint64_t window     = std::strtoull(find_arg(argc, argv, "--window", "50000"), nullptr, 10);
  uint64_t max_cycles = std::strtoull(find_arg(argc, argv, "--max-cycles", "45000000"), nullptr, 10);
  constexpr uint64_t kRegDumpStride = 200;  // full 4-core reg dump every N post-trigger cycles

  Vsystem *tb = new Vsystem;
  bind_registers(tb);

  Verilated::traceEverOn(true);
  VerilatedVcdC *tfp = new VerilatedVcdC;
  tb->trace(tfp, 99);
  tfp->open(vcd_path);

  unsigned long long tickcount = 0ULL;
  reset(tb, tickcount);
  if (!load_image(tb, std::string(image), tickcount, "[seqlock_wedge_trace]")) {
    delete tb;
    return 2;
  }

  std::printf("[seqlock_wedge_trace] watching for a hart PC stuck >= %llu cycles; "
              "on trigger, dumping VCD+reglog for %llu more cycles (cap %llu)\n",
              (unsigned long long)threshold, (unsigned long long)window,
              (unsigned long long)max_cycles);
  std::printf("[seqlock_wedge_trace] vcd=%s reglog=%s\n", vcd_path, reglog_path);
  std::fflush(stdout);

  uint64_t prev_pc[4]     = {};
  uint64_t streak[4]      = {};
  bool     triggered      = false;
  uint64_t trigger_cycle  = 0;
  unsigned dump_tick      = 0;
  FILE    *reglog         = nullptr;

  for (int c = 0; c < 4; ++c) prev_pc[c] = core_pc(tb, c);

  using clock = std::chrono::steady_clock;
  auto t_start = clock::now();
  auto t_last  = t_start;

  uint64_t cycle = 0;
  for (; cycle < max_cycles; ++cycle) {
    if (triggered) {
      tick_dump(tb, tfp, ++dump_tick);
    } else {
      tb->eval();
      tb->clock = 1; tb->eval();
      tb->clock = 0; tb->eval();
    }
    ++tickcount;

    uint64_t pc[4];
    bool     commit[4];
    for (int c = 0; c < 4; ++c) {
      pc[c] = core_pc(tb, c);
      commit[c] = core_commit(tb, c);
      if (pc[c] == prev_pc[c]) ++streak[c]; else { streak[c] = 0; prev_pc[c] = pc[c]; }
    }

    if (!triggered) {
      for (int c = 0; c < 4; ++c) {
        if (streak[c] >= threshold) {
          triggered = true;
          trigger_cycle = cycle;
          reglog = std::fopen(reglog_path, "w");
          std::fprintf(reglog,
            "WEDGE DETECTED: core%d PC stuck at 0x%016lx for >=%llu cycles "
            "(triggered at sim cycle %llu)\n",
            c, (unsigned long)pc[c], (unsigned long long)threshold,
            (unsigned long long)cycle);
          std::fprintf(reglog, "pc0=0x%016lx pc1=0x%016lx pc2=0x%016lx pc3=0x%016lx\n",
                       (unsigned long)pc[0], (unsigned long)pc[1],
                       (unsigned long)pc[2], (unsigned long)pc[3]);
          write_regs(reglog, cycle);
          std::fprintf(stderr,
            "\n[seqlock_wedge_trace] WEDGE DETECTED: core%d parked at 0x%016lx "
            "for >=%llu cycles (cycle %llu) -- capturing %llu more cycles\n",
            c, (unsigned long)pc[c], (unsigned long long)threshold,
            (unsigned long long)cycle, (unsigned long long)window);
          break;
        }
      }
    }

    if (triggered) {
      std::fprintf(reglog,
        "cyc %llu  c0=0x%016lx(%d) c1=0x%016lx(%d) c2=0x%016lx(%d) c3=0x%016lx(%d)\n",
        (unsigned long long)cycle,
        (unsigned long)pc[0], commit[0], (unsigned long)pc[1], commit[1],
        (unsigned long)pc[2], commit[2], (unsigned long)pc[3], commit[3]);
      if ((cycle - trigger_cycle) % kRegDumpStride == 0) write_regs(reglog, cycle);

      if (cycle - trigger_cycle >= window) {
        std::fprintf(stderr,
          "\n[seqlock_wedge_trace] capture window complete at cycle %llu "
          "(triggered at %llu); VCD=%s reglog=%s\n",
          (unsigned long long)cycle, (unsigned long long)trigger_cycle,
          vcd_path, reglog_path);
        break;
      }
    }

    auto now = clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last).count() >= 5000) {
      double total = std::chrono::duration<double>(now - t_start).count();
      std::fprintf(stderr,
        "[seqlock_wedge_trace] +%5.0fs  cycles=%-12llu  %s  "
        "pc0=0x%016lx pc1=0x%016lx pc2=0x%016lx pc3=0x%016lx  "
        "streak=[%llu,%llu,%llu,%llu]\n",
        total, (unsigned long long)cycle, triggered ? "DUMPING" : "watching",
        (unsigned long)pc[0], (unsigned long)pc[1],
        (unsigned long)pc[2], (unsigned long)pc[3],
        (unsigned long long)streak[0], (unsigned long long)streak[1],
        (unsigned long long)streak[2], (unsigned long long)streak[3]);
      std::fflush(stderr);
      t_last = now;
    }
  }

  if (!triggered) {
    std::fprintf(stderr,
      "\n[seqlock_wedge_trace] reached max-cycles (%llu) with no wedge detected\n",
      (unsigned long long)max_cycles);
  }

  if (reglog) std::fclose(reglog);
  tfp->close();
  tb->final();
  delete tb;
  return triggered ? 0 : 1;
}
