// lockstep_quad.cpp — 4-hart lock-step (RTL vs golden emulator) for q4 benches.
//
// Same CLI / logs / flags as lockstep.cpp, but every committing hart is
// compared — not just core 0. Shared-memory races that are legal under
// commit-order interleaving (load / LR / SC / AMO rd disagreement) are
// reconciled the same way as lockstep_linux.cpp.
//
// Usage:
//   ./lockstep_quad.out --image <path>
//                       [--done-pc <hex> ...]
//                       [--done-a0 <val>]
//                       [--logdir <dir>]
//                       [--show-state]
//                       [--dump-waves]
//
// Exit: 0 = completion reached, 1 = mismatch / timeout.
//
// Logs under --logdir (default .), one blank-separated frame per tick:
//   run.log     cycle, then 4 lines of "hart pc insn"
//   states.log  4 lines of "hart pc insn"
//   regs.log    4 cores of "hart pc" + 32 GPRs (same layout as lockstep.cpp)
//   system_trace.vcd   only with --dump-waves

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <cstring>
#include <stdint.h>
#include <iomanip>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <termios.h>

#define LOCKSTEP
#define MISA_SPEC (0b100000001000100000001 | (0b1llu << 63))
#include "sim/emulator/emulator.h"
#undef SHOW_TERMINAL
#include "sim/rtl/rtl_model.h"

#include "sim/harness/common/args.h"
#include "sim/harness/common/completion.h"

using namespace std;
using namespace harness;

#define PROBE_DOUBLE ((0xCA3BF0UL + (-136)) & (~7UL))

emulator golden_model;

void signal_callback_handler(int signum) {
  for (int h = 0; h < 4; ++h) {
    cout << "---- hart " << h << " ----\n";
    golden_model.show_state(h);
  }
  tcflush(0, TCIFLUSH);
  exit(signum);
}

static uint32_t msip_latch[4] = {0, 0, 0, 0};
static int pending_mtime_rd[4] = {-1, -1, -1, -1};
static constexpr uint64_t kClintMtime = 0x0200bff8ULL;

struct MemOp {
  bool valid = false;
  int kind = 0;  // 0 none, 1 load, 2 LR, 3 SC, 4 AMO
  int rd = 0;
  int width = 8;
  uint32_t funct5 = 0;
  uint64_t addr = 0;
  uint64_t rs2val = 0;
  uint64_t oldword = 0;
};
static MemOp last_memop[4];
static uint64_t racy_fixups = 0;

static uint64_t amo_result(uint32_t funct5, int width, uint64_t oldv,
                           uint64_t rs2) {
  auto sext32 = [](uint64_t v) { return (uint64_t)(int64_t)(int32_t)v; };
  if (width == 4) { oldv = sext32(oldv); rs2 = sext32(rs2); }
  switch (funct5) {
    case 0x00: return oldv + rs2;
    case 0x01: return rs2;
    case 0x04: return oldv ^ rs2;
    case 0x08: return oldv | rs2;
    case 0x0c: return oldv & rs2;
    case 0x10: return ((int64_t)oldv < (int64_t)rs2) ? oldv : rs2;
    case 0x14: return ((int64_t)oldv > (int64_t)rs2) ? oldv : rs2;
    case 0x18: return (oldv < rs2) ? oldv : rs2;
    case 0x1c: return (oldv > rs2) ? oldv : rs2;
    default:   return oldv;
  }
}

static uint64_t splice_word(uint64_t word8, uint64_t addr, int width,
                            uint64_t value) {
  if (width == 8) return value;
  if (addr & 4)
    return (word8 & 0x00000000ffffffffULL) | (value << 32);
  return (word8 & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
}

static void apply_pending_mtime_rd(int h, simulator &bench) {
  if (pending_mtime_rd[h] < 0) return;
  const int rd = pending_mtime_rd[h];
  pending_mtime_rd[h] = -1;
  if (rd == 0) return;
  golden_model.set_register_with_value(
      (uint8_t)rd, bench.get_register_value(h, (uint8_t)rd), h);
}

static void golden_step_hart(int h, simulator &bench) {
  golden_model.clear_wfi(h);

  if (!bench.msip(h))
    msip_latch[h] = 0;
  golden_model.set_msip(h, msip_latch[h]);

  const uint32_t insn_before = golden_model.get_instruction(h);
  int mtime_rd = -1;
  last_memop[h].valid = false;
  if ((insn_before & 0x7fu) == 0x03u) {
    const int rs1 = (insn_before >> 15) & 0x1f;
    const int rd = (insn_before >> 7) & 0x1f;
    const int64_t iimm = (int64_t)(int32_t)insn_before >> 20;
    const uint64_t addr = golden_model.reg_file(h)[rs1] + (uint64_t)iimm;
    if ((addr & ~7ULL) == kClintMtime)
      mtime_rd = rd;
    if (rd != 0 && addr >= 0x80000000ULL)
      last_memop[h] = {true, 1, rd, 8, 0, addr, 0, 0};
  } else if ((insn_before & 0x7fu) == 0x2fu) {
    const int rd = (insn_before >> 7) & 0x1f;
    const int rs1 = (insn_before >> 15) & 0x1f;
    const int rs2 = (insn_before >> 20) & 0x1f;
    const uint32_t funct5 = insn_before >> 27;
    const int width = ((insn_before >> 12) & 7) == 2 ? 4 : 8;
    const uint64_t addr = golden_model.reg_file(h)[rs1];
    const int kind = (funct5 == 0x02) ? 2 : (funct5 == 0x03) ? 3 : 4;
    if (rd != 0 && addr >= 0x80000000ULL) {
      last_memop[h] = {true, kind, rd, width, funct5, addr,
                       golden_model.reg_file(h)[rs2],
                       golden_model.read_mem64(addr & ~7ULL)};
    }
  }

  golden_model.step_hart_only(h);
  while (((golden_model.get_instruction(h) & 0x7f) == 0x73) &&
         (golden_model.get_instruction(h) & 0x7000)) {
    golden_model.step_hart_only(h);
  }

  if (insn_before == 0x10500073u)
    msip_latch[h] = bench.msip(h);
  if (mtime_rd >= 0)
    pending_mtime_rd[h] = mtime_rd;
}

static bool force_take_if_needed(int h, simulator &bench) {
  const uint64_t rp = bench.last_commit_pc[h];
  const uint64_t gp = golden_model.get_pc(h);
  const uint64_t tv = bench.mtvec(h);
  const bool rtl_in_trap_entry = (tv != 0) && (rp >= tv && rp < tv + 16);
  const bool golden_still_guest = !(gp >= tv && gp < tv + 16);
  if (rp == gp || !golden_still_guest) return false;
  if (!(bench.commit_interrupt(h) || rtl_in_trap_entry)) return false;

  if (bench.mcause(h) == 0x8000000000000003ULL)
    golden_model.take_machine_soft_irq(h);
  else
    golden_model.take_machine_timer_irq(h);

  const uint64_t rtl_mepc = bench.mepc(h);
  if (rtl_mepc != 0)
    golden_model.set_mepc(h, rtl_mepc);

  for (int g = 0; g < 32; ++g) {
    const uint64_t gpc = golden_model.get_pc(h);
    if (gpc == rp) break;
    if (!(gpc >= tv && gpc < tv + 16 && gpc < rp)) break;
    golden_model.step_hart_only(h);
    while (((golden_model.get_instruction(h) & 0x7f) == 0x73) &&
           (golden_model.get_instruction(h) & 0x7000)) {
      golden_model.step_hart_only(h);
    }
  }
  if (golden_model.get_pc(h) == rp) {
    for (int r = 1; r <= 31; ++r) {
      golden_model.set_register_with_value(
          (uint8_t)r, bench.read_register(h, r), h);
    }
    golden_model.set_mstatus(h, bench.read_register(h, 32));
  }
  return true;
}

static bool reconcile_racy(int h, int r, uint64_t rtl_pc, simulator &bench) {
  if (r < 1 || r > 31 || !last_memop[h].valid || r != last_memop[h].rd)
    return false;
  const MemOp &op = last_memop[h];
  const uint64_t rtl_rd = bench.read_register(h, r);
  const uint64_t gold_rd = golden_model.reg_file(h)[r];
  golden_model.set_register_with_value((uint8_t)r, rtl_rd, h);
  if (op.kind == 3) {
    const uint64_t w8 = golden_model.read_mem64(op.addr & ~7ULL);
    if (rtl_rd == 0 && gold_rd != 0) {
      golden_model.write_mem64(op.addr & ~7ULL,
          splice_word(w8, op.addr, op.width, op.rs2val));
    } else if (rtl_rd != 0 && gold_rd == 0) {
      golden_model.write_mem64(op.addr & ~7ULL, op.oldword);
    }
  } else if (op.kind == 4) {
    const uint64_t neww = amo_result(op.funct5, op.width, rtl_rd, op.rs2val);
    const uint64_t w8 = golden_model.read_mem64(op.addr & ~7ULL);
    golden_model.write_mem64(op.addr & ~7ULL,
        splice_word(w8, op.addr, op.width, neww));
  }
  ++racy_fixups;
  static map<uint64_t, uint32_t> fixup_seen;
  const uint64_t site = ((uint64_t)h << 62) | rtl_pc;
  const uint32_t n_site = ++fixup_seen[site];
  if (n_site <= 5 || (racy_fixups % 1000) == 0)
    cout << "[racy-fixup #" << dec << racy_fixups << "] h" << h
         << " pc=0x" << hex << rtl_pc << " kind=" << dec << op.kind
         << " x" << r << " rtl=0x" << hex << rtl_rd
         << " golden=0x" << gold_rd << " addr=0x" << op.addr << dec
         << " site_n=" << n_site << endl;
  return true;
}

// One lock-step tick: cycle, then cores 0-3, then a blank line.
// snprintf so sticky iostream hex/setw cannot glue fields together.
static void log_quad_step(ostream &run, ostream &st, ostream &regs,
                          simulator &bench, uint8_t mask) {
  char line[96];
  std::snprintf(line, sizeof(line), "%016llu\n",
                (unsigned long long)bench.cycles());
  run << line;
  st << line;
  regs << line;
  for (int h = 0; h < 4; ++h) {
    const uint64_t pc = (mask & (1u << h)) ? bench.last_commit_pc[h]
                                           : bench.core_pc(h);
    const uint32_t insn = golden_model.get_instruction(h);
    std::snprintf(line, sizeof(line), "c%d %016llx %08x\n",
                  h, (unsigned long long)pc, insn);
    run << line;
    st << line;
    std::snprintf(line, sizeof(line), "c%d %016llx\n",
                  h, (unsigned long long)pc);
    regs << line << bench.return_registers(h);
  }
  run << '\n';
  st << '\n';
  regs << '\n';
}

static void dump_mismatch(const char *kind, uint64_t matched,
                          simulator &bench, int hart) {
  cout << kind << " after " << dec << matched
       << " matched commits (hart " << hart << ")\n";
  cout << "  cycle: " << dec << bench.cycles() << endl;
  cout << "  RTL PCs:    ";
  for (int h = 0; h < 4; ++h)
    cout << "pc" << h << "=0x" << hex << bench.core_pc(h) << " ";
  cout << "\n  golden PCs: ";
  for (int h = 0; h < 4; ++h)
    cout << "pc" << h << "=0x" << hex << golden_model.get_pc(h) << " ";
  cout << "\n";
}

int main(int argc, char *argv[]) {
  struct tm current_time;
  time_t now = time(NULL);
  localtime_r(&now, &current_time);

  const char *image_path = find_arg(argc, argv, "--image", "sim/data/Image");
  const char *logdir     = find_arg(argc, argv, "--logdir", ".");
  bool show_state        = has_flag(argc, argv, "--show-state");
  bool dump_waves        = has_flag(argc, argv, "--dump-waves");
  Completion completion  = Completion::parse(argc, argv);

  string logp = string(logdir);
  if (!logp.empty() && logp.back() != '/') logp += "/";
  string vcd_path = logp + "system_trace.vcd";

  simulator bench;
  bench.init(image_path, "sim/data/qemu.dtb", "sim/data/boot.bin",
             dump_waves, vcd_path);
  printf("bench initiated! image=%s (4-hart lock-step)\n", image_path);
  if (dump_waves) printf("[lockstep-q4] VCD → %s\n", vcd_path.c_str());
  cout << endl;

  golden_model.init(image_path);

  ofstream outFile((logp + "run.log").c_str());
  ofstream outState((logp + "states.log").c_str());
  ofstream outregs((logp + "regs.log").c_str());
  if (!outFile.is_open() || !outState.is_open() || !outregs.is_open()) {
    cerr << "Error opening log files under " << logp << endl;
    return 1;
  }
  outFile  << "# lockstep-q4 run.log  — 4-hart commit trace\n"
           << "# cycle\n"
           << "# core pc insn\n";
  outState << "# lockstep-q4 states.log  — 4-hart pc+insn each tick\n"
           << "# cycle\n"
           << "# core pc insn\n";
  outregs  << "# lockstep-q4 regs.log  — all 4 cores RTL GPRs each tick\n"
           << "# cycle\n"
           << "# core pc\n"
           << "# x0 .. x31\n";

  signal(SIGINT, signal_callback_handler);
  bench.set_probe(PROBE_DOUBLE);

  printf("Runtime: %04d-%02d-%02d %02d:%02d:%02d\n",
    current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday,
    current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
  printf("Lock-step: all 4 harts PC+regs vs golden\n");

  uint64_t matched = 0;
  uint64_t matched_per_hart[4] = {};

  while (1) {
    uint8_t mask = 0;
    int x = bench.step_until_commits(dump_waves, &mask);
    if (x == 1) {
      dump_mismatch("Stall", matched, bench, 0);
      break;
    }

    golden_model.set_mtime(bench.mtime());
    for (int h = 0; h < 4; ++h)
      golden_model.set_mtimecmp(h, bench.mtimecmp(h));

    log_quad_step(outFile, outState, outregs, bench, mask);
    if (show_state) {
      for (int h = 0; h < 4; ++h)
        golden_model.show_state(h);
      cout << "\n";
    }

    bool failed = false;
    bool done = false;
    for (int h = 0; h < 4; ++h) {
      if (!(mask & (1u << h))) continue;

      apply_pending_mtime_rd(h, bench);
      force_take_if_needed(h, bench);

      const uint64_t rtl_pc = bench.last_commit_pc[h];
      const uint64_t emu_pc = golden_model.get_pc(h);
      const uint32_t insn   = golden_model.get_instruction(h);

      if (rtl_pc != emu_pc) {
        dump_mismatch("PC mismatch", matched, bench, h);
        cout << "  emulator PC: 0x" << hex << emu_pc
             << "  insn: 0x" << setfill('0') << setw(8) << insn << "\n";
        cout << "  RTL commit:  0x" << hex << rtl_pc << "\n";
        golden_model.show_state(h);
        if (dump_waves) {
          cout << "  dumping 50 post-mismatch cycles into " << vcd_path
               << " ...\n";
          for (int i = 0; i < 50; ++i) bench.step();
        }
        failed = true;
        break;
      }

      int r = bench.check_registers(h, golden_model.reg_file(h),
                                    golden_model.get_mstatus(h));
      if (r && reconcile_racy(h, r, rtl_pc, bench)) {
        r = bench.check_registers(h, golden_model.reg_file(h),
                                  golden_model.get_mstatus(h));
      }
      if (r) {
        dump_mismatch("Register mismatch", matched, bench, h);
        cout << "  register x" << dec << r
             << "  RTL=0x" << setfill('0') << setw(16) << hex
             << bench.read_register(h, r) << "\n"
             << "  golden=0x" << setfill('0') << setw(16) << hex
             << (r == 32 ? golden_model.get_mstatus(h)
                         : golden_model.reg_file(h)[r]) << "\n";
        for (int i = 1; i <= 31; ++i) {
          const uint64_t rv = bench.read_register(h, i);
          const uint64_t gv = golden_model.reg_file(h)[i];
          if (rv != gv)
            cout << "    diff x" << dec << i << ": RTL=0x" << hex << rv
                 << " golden=0x" << gv << "\n";
        }
        golden_model.show_state(h);
        if (dump_waves) {
          cout << "  dumping 50 post-mismatch cycles into " << vcd_path
               << " ...\n";
          for (int i = 0; i < 50; ++i) bench.step();
        }
        failed = true;
        break;
      }

      matched++;
      matched_per_hart[h]++;
      golden_step_hart(h, bench);

      // Coordinator (hart 0) hitting the done-PC is the benchmark exit,
      // matching profile_quad. Other harts may already be parked there.
      if (h == 0 && completion.hit(rtl_pc, bench.get_register_value(0, 10))) {
        done = true;
      }
    }

    if (failed) {
      outFile.close();
      tcflush(0, TCIFLUSH);
      return 1;
    }
    if (done) {
      printf("Test complete\n");
      printf("Matched commits: %llu (h0=%llu h1=%llu h2=%llu h3=%llu)  racy=%llu\n",
             (unsigned long long)matched,
             (unsigned long long)matched_per_hart[0],
             (unsigned long long)matched_per_hart[1],
             (unsigned long long)matched_per_hart[2],
             (unsigned long long)matched_per_hart[3],
             (unsigned long long)racy_fixups);
      outFile.close();
      tcflush(0, TCIFLUSH);
      return 0;
    }

    if ((matched % 1000000ULL) == 0 && matched > 0) {
      cout << "[lockstep-q4] " << dec << matched << " matched, cycle "
           << bench.cycles() << " per_hart=["
           << matched_per_hart[0] << "," << matched_per_hart[1] << ","
           << matched_per_hart[2] << "," << matched_per_hart[3] << "]\n";
      cout.flush();
    }
  }

  printf("Test failed: Time-out!\n");
  printf("Total ticks: %ld\n", (long)bench.cycles());
  printf("Matched commits: %llu (h0=%llu h1=%llu h2=%llu h3=%llu)\n",
         (unsigned long long)matched,
         (unsigned long long)matched_per_hart[0],
         (unsigned long long)matched_per_hart[1],
         (unsigned long long)matched_per_hart[2],
         (unsigned long long)matched_per_hart[3]);
  outFile.close();
  tcflush(0, TCIFLUSH);
  return 1;
}
