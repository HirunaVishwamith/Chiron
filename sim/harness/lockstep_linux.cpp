// lockstep_linux.cpp — multi-hart RTL vs golden lock-step of a Linux image.
//
// Tracks ALL four harts (not just core0). Under the RISC-V lottery, core0 is
// often a secondary parked in clear_bss_done spinwait; a core0-only lockstep
// then matches forever while the real boot hart can diverge or hang. On any
// PC/register mismatch we dump all four RTL + golden PCs, a ring of recent
// commits to run.log, golden state for the offending hart, and a short
// post-mismatch VCD window (build/lockstep_linux.vcd).
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#define LOCKSTEP
#define MISA_SPEC (0b100000001000100000001 | (0b1llu << 63))
#include "sim/emulator/emulator.h"
// Stream UART TX from all harts into the lockstep log so we can see how far
// Linux got when a PC mismatch or stall fires.
#define SHOW_TERMINAL
#include "sim/rtl/rtl_model.h"
#include <chrono>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <termios.h>
#include <iomanip>
#include <cstdio>

using namespace std;

emulator golden_model;

void signal_callback_handler(int signum) {
  golden_model.show_state(0);
  tcflush(0, TCIFLUSH);
  exit(signum);
}

// Step one golden hart after an RTL commit of the same hart. Mirrors the
// single-core path: peripheral MMIO loads take the value from RTL; CSR-like
// SYSTEM ops that the RTL hides from commit are stepped through on golden.
// Per-hart MSIP view for golden: sample RTL msipShared only at WFI boundaries
// (and force-clear when RTL clears). Live sampling every step lets golden see
// an IPI mid-poll (csrr mip) before RTL's in-flight csrr did, so golden takes
// the MSIP path while RTL branches back to WFI → PC mismatch at 0x800002e4.
static uint32_t msip_latch[4] = {0, 0, 0, 0};

// After golden executes a load of CLINT mtime, force that rd from RTL on the
// *next* commit of the same hart (registersOut has then settled to the
// post-load value). RTL samples mtime at execute; golden would otherwise read
// a free-running counter one tick later → clint_get_cycles64 x10 off-by-1.
static int pending_mtime_rd[4] = {-1, -1, -1, -1};

static constexpr uint64_t kClintMtime = 0x0200bff8ULL;

// ── Racy shared-memory op tracking (multi-hart lock-step) ───────────────────
// The RTL is OOO and truly parallel; golden serializes the four harts at
// commit granularity. Two divergences are architecturally legal and NOT RTL
// bugs:
//   * a plain load / LR whose RTL execution sampled memory earlier than its
//     commit order (another hart's store lands in between), and
//   * an SC / AMO whose success or read-modify-write base differs because
//     golden's reservation was broken by an interleaved remote commit.
// When the (single) mismatching register is the rd of the op golden just
// executed, adopt the RTL's outcome: patch rd, and for SC/AMO patch golden's
// memory word so both models agree going forward. Every event is logged; a
// real RTL load/AMO bug would show up as a flood of these, not a trickle.
struct MemOp {
  bool valid = false;
  int kind = 0;  // 0 none, 1 load, 2 LR, 3 SC, 4 AMO
  int rd = 0;
  int width = 8;          // 4 (.w) or 8 (.d)
  uint32_t funct5 = 0;    // AMO op selector
  uint64_t addr = 0;
  uint64_t rs2val = 0;    // SC/AMO store operand
  uint64_t oldword = 0;   // golden 8-byte word at addr BEFORE the op
};
static MemOp last_memop[4];
static uint64_t racy_fixups = 0;

// Recompute an AMO's memory result from the RTL's observed old value.
static uint64_t amo_result(uint32_t funct5, int width, uint64_t oldv,
                           uint64_t rs2) {
  auto sext32 = [](uint64_t v) { return (uint64_t)(int64_t)(int32_t)v; };
  if (width == 4) { oldv = sext32(oldv); rs2 = sext32(rs2); }
  switch (funct5) {
    case 0x00: return oldv + rs2;                       // amoadd
    case 0x01: return rs2;                              // amoswap
    case 0x04: return oldv ^ rs2;                       // amoxor
    case 0x08: return oldv | rs2;                       // amoor
    case 0x0c: return oldv & rs2;                       // amoand
    case 0x10: return ((int64_t)oldv < (int64_t)rs2) ? oldv : rs2;  // amomin
    case 0x14: return ((int64_t)oldv > (int64_t)rs2) ? oldv : rs2;  // amomax
    case 0x18: return (oldv < rs2) ? oldv : rs2;        // amominu
    case 0x1c: return (oldv > rs2) ? oldv : rs2;        // amomaxu
    default:   return oldv;
  }
}

// Splice a 4- or 8-byte value into golden's aligned 8-byte word at addr.
static uint64_t splice_word(uint64_t word8, uint64_t addr, int width,
                            uint64_t value) {
  if (width == 8) return value;
  if (addr & 4)
    return (word8 & 0x00000000ffffffffULL) | (value << 32);
  return (word8 & 0xffffffff00000000ULL) | (value & 0xffffffffULL);
}

static void apply_pending_mtime_rd(int h, simulator &bench) {
  if (pending_mtime_rd[h] < 0)
    return;
  const int rd = pending_mtime_rd[h];
  pending_mtime_rd[h] = -1;
  if (rd == 0)
    return;
  golden_model.set_register_with_value(
      (uint8_t)rd, bench.get_register_value(h, (uint8_t)rd), h);
}

static void golden_step_hart(int h, simulator &bench) {
  golden_model.clear_wfi(h);

  // Publish latched MSIP into golden CLINT before the step (csrr mip reads it).
  if (!bench.msip(h))
    msip_latch[h] = 0;
  golden_model.set_msip(h, msip_latch[h]);

  const uint32_t insn_before = golden_model.get_instruction(h);
  int mtime_rd = -1;
  last_memop[h].valid = false;
  if ((insn_before & 0x7fu) == 0x03u) {  // LOAD
    const int rs1 = (insn_before >> 15) & 0x1f;
    const int rd = (insn_before >> 7) & 0x1f;
    const int64_t iimm = (int64_t)(int32_t)insn_before >> 20;
    const uint64_t addr =
        golden_model.reg_file(h)[rs1] + (uint64_t)iimm;
    if ((addr & ~7ULL) == kClintMtime)
      mtime_rd = rd;
    if (rd != 0 && addr >= 0x80000000ULL) {
      last_memop[h] = {true, /*kind=*/1, rd, 8, 0, addr, 0, 0};
    }
  } else if ((insn_before & 0x7fu) == 0x2fu) {  // AMO / LR / SC
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
  // CSR-skip (matches suppressed robOut commitFired for SYSTEM ops).
  while (((golden_model.get_instruction(h) & 0x0000007f) == 0x73) &&
         (golden_model.get_instruction(h) & 0x00007000)) {
    golden_model.step_hart_only(h);
  }

  // After WFI (0x10500073), resync latch from live RTL MSIP.
  if (insn_before == 0x10500073u)
    msip_latch[h] = bench.msip(h);

  if (mtime_rd >= 0)
    pending_mtime_rd[h] = mtime_rd;
}

static void dump_mismatch_header(const char *kind, uint64_t matched,
                                 simulator &bench, int hart) {
  cout << kind << " after " << dec << matched
       << " matched commits (hart " << hart << ")\n";
  cout << "  cycle: " << dec << (bench.tickcount + bench.dump_tick) << endl;
  cout << "  RTL PCs: "
       << "pc0=0x" << hex << bench.core_pc(0)
       << " pc1=0x" << hex << bench.core_pc(1)
       << " pc2=0x" << hex << bench.core_pc(2)
       << " pc3=0x" << hex << bench.core_pc(3) << endl;
  cout << "  golden PCs: "
       << "pc0=0x" << hex << golden_model.get_pc(0)
       << " pc1=0x" << hex << golden_model.get_pc(1)
       << " pc2=0x" << hex << golden_model.get_pc(2)
       << " pc3=0x" << hex << golden_model.get_pc(3) << endl;
  cout << "  hart " << dec << hart << " CSRs:"
       << " RTL mstatus=0x" << hex << bench.read_register(hart, 32)
       << " golden mstatus=0x" << golden_model.get_mstatus(hart)
       << " | RTL mepc=0x" << bench.mepc(hart)
       << " golden mepc=0x" << golden_model.get_mepc(hart)
       << " | RTL mcause=0x" << bench.mcause(hart)
       << " mtvec=0x" << bench.mtvec(hart) << endl;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <ignored> <dtb> <bootrom>\n", argv[0]);
    return 1;
  }

  simulator bench;
  const char *vcd_path = "build/lockstep_linux.vcd";
  // Trace enabled so a post-mismatch window can be recorded; per-cycle dump
  // stays off (step_until_commits_nodump) until mismatch.
  bench.init("sim/data/Image", argv[2], argv[3], /*enable_trace=*/true,
              vcd_path);
  printf("bench inititated! (VCD -> %s)\n\n", vcd_path);

  golden_model.init("sim/data/Image");

  std::ofstream outFile("run.log");
  if (!outFile.is_open()) {
    std::cerr << "Error opening run.log" << std::endl;
    return 1;
  }
  const int RING = 128;
  struct {
    uint64_t cycle;
    int hart;
    uint64_t emu_pc, rtl_pc, insn;
  } ring[RING];
  int ring_i = 0, ring_n = 0;

  signal(SIGINT, signal_callback_handler);
  bench.set_probe((0x2004000UL + 0x0UL) & (~7UL));

  uint64_t matched = 0;
  uint64_t matched_per_hart[4] = {};
  printf("Simulation start time: %s %s\n", __DATE__, __TIME__);
  printf("Lock-step: ALL 4 harts PC+regs vs golden; mismatch -> PCs + VCD + "
         "run.log\n");

  while (true) {
    uint8_t mask = 0;
    int x = bench.step_until_commits_nodump(&mask);
    if (x == 1) {
      cout << "TIMEOUT: no commit on any hart for STEP_TIMEOUT cycles\n";
      dump_mismatch_header("Stall", matched, bench, 0);
      outFile << "=== STALL ring ===\n";
      for (int k = 0; k < ring_n; ++k) {
        auto &e = ring[(ring_i + RING - ring_n + k) % RING];
        outFile << dec << e.cycle << " h" << e.hart
                << " emu_pc=0x" << hex << e.emu_pc
                << " rtl_pc=0x" << e.rtl_pc << " insn=0x" << e.insn << "\n";
      }
      outFile.flush();
      cout << "  dumping 200 stall cycles into " << vcd_path << " ..." << endl;
      for (int i = 0; i < 200; ++i) bench.step();
      break;
    }

    // NOTE: under #define LOCKSTEP, emulator::set_interrupts/deliver_interrupts
    // *also* hart_step each target (legacy single-core harness quirk). Never call
    // them here — only step the harts that actually committed on RTL.
    //
    // Do NOT independently advance golden mtime (tick_only). RTL MultiUart mtime
    // uses a 16-cycle prescaler; golden advance(1) per h0 commit drifts, so
    // clint_get_cycles64's ld of mtime fails lockstep (x10 RTL≠golden ~20M).
    // Mirror RTL free-running mtime (+ mtimecmp so MTIP checks stay consistent).
    golden_model.set_mtime(bench.mtime());
    for (int h = 0; h < 4; ++h)
      golden_model.set_mtimecmp(h, bench.mtimecmp(h));

    bool failed = false;
    for (int h = 0; h < 4; ++h) {
      if (!(mask & (1u << h))) continue;

      // Settle golden mtime-load rd from RTL before PC/reg compare.
      apply_pending_mtime_rd(h, bench);

      bool force_took = false;      // this commit went through IRQ force-take
      bool force_resynced = false;  // ... and GPRs were adopted from RTL

      // RTL timer/MSIP IRQ: either commit_interrupt pulses, or the next retired
      // PC is at the trap vector while golden is still in the interrupted basic
      // block. Deliver the same trap on golden so lock-step re-converges
      // (auto-take is disabled under LOCKSTEP). The window is mtvec..mtvec+16 —
      // exactly the handle_exception entry instructions. A wide 0x8020xxxx
      // range heuristic misfired on ordinary kernel text nearby (fork path
      // arch_dup_task_struct at 0x8020206c) and blocked a legitimate take.
      {
        const uint64_t rp = bench.last_commit_pc[h];
        const uint64_t gp = golden_model.get_pc(h);
        const uint64_t tv = bench.mtvec(h);
        const bool rtl_in_trap_entry =
            (tv != 0) && (rp >= tv && rp < tv + 16);
        const bool golden_still_guest = !(gp >= tv && gp < tv + 16);
        // Only force-take when golden is still on the interrupted guest path.
        // Do NOT step golden toward RTL mepc first: those guest ops were already
        // matched on earlier commits (double-exec desyncs to e.g. 0x8026f404).
        if (rp != gp && golden_still_guest &&
            (bench.commit_interrupt(h) || rtl_in_trap_entry)) {
          force_took = true;
          // interrupt_function(mepc:=golden PC). RTL may have interrupted a few
          // instrs later → mepc/x18 skew. After trap catch-up, resync mepc+GPRs
          // from RTL so trap-prologue state (sp/x2, csrr mepc→x18) matches.
          const uint64_t gp_before_take = golden_model.get_pc(h);
          // Deliver the same cause the RTL took: during SMP bring-up the trap
          // is often an MSIP IPI, and a hardwired timer cause would make the
          // handler's csrr mcause diverge.
          if (bench.mcause(h) == 0x8000000000000003ULL)
            golden_model.take_machine_soft_irq(h);
          else
            golden_model.take_machine_timer_irq(h);
          // interrupt_function is a silent no-op when golden mstatus.mie==0
          // (hart_trap.inc); surface that instead of dying on an opaque
          // PC mismatch a commit later.
          if (golden_model.get_pc(h) == gp_before_take)
            cout << "[force-take] h" << dec << h
                 << " NO-OP (golden mie=0?) at emu_pc=0x" << hex
                 << gp_before_take << " rtl_pc=0x" << rp
                 << " golden mstatus=0x" << golden_model.get_mstatus(h)
                 << dec << endl;
          const uint64_t rtl_mepc = bench.mepc(h);
          if (rtl_mepc != 0)
            golden_model.set_mepc(h, rtl_mepc);
          // take_* sets PC to mtvec; RTL may already have retired mtvec+4..
          for (int g = 0; g < 32; ++g) {
            const uint64_t gpc = golden_model.get_pc(h);
            if (gpc == rp)
              break;
            if (!(gpc >= tv && gpc < tv + 16 && gpc < rp))
              break;
            golden_model.step_hart_only(h);
            while (((golden_model.get_instruction(h) & 0x7f) == 0x73) &&
                   (golden_model.get_instruction(h) & 0x7000)) {
              golden_model.step_hart_only(h);
            }
          }
          // Residual guest-timing skew (sp off by a frame, etc.): once PCs
          // match in trap entry, adopt RTL architectural GPRs for this hart.
          if (golden_model.get_pc(h) == rp) {
            force_resynced = true;
            for (int r = 1; r <= 31; ++r) {
              golden_model.set_register_with_value(
                  (uint8_t)r, bench.read_register(h, r), h);
            }
            // mstatus is compared every commit (index 32) but isn't covered
            // by the GPR loop; the same take-point skew that shifts sp/x18
            // can shift MPIE/MIE capture. Adopt RTL's mstatus too.
            golden_model.set_mstatus(h, bench.read_register(h, 32));
          }
        }
      }

      uint64_t rtl_pc = bench.last_commit_pc[h];
      uint64_t emu_pc = golden_model.get_pc(h);

      {
        auto &e = ring[ring_i];
        e.cycle = bench.tickcount + bench.dump_tick;
        e.hart = h;
        e.emu_pc = emu_pc;
        e.rtl_pc = rtl_pc;
        e.insn = golden_model.get_instruction(h);
        ring_i = (ring_i + 1) % RING;
        if (ring_n < RING) ring_n++;
      }

      if (rtl_pc != emu_pc) {
        dump_mismatch_header("PC mismatch", matched, bench, h);
        cout << "  emulator PC:  0x" << hex << emu_pc;
        cout << "  insn: 0x" << setfill('0') << setw(8) << hex
             << golden_model.get_instruction(h) << endl;
        cout << "  RTL commit PC: 0x" << hex << rtl_pc << endl;
        outFile << "=== PC MISMATCH ring (oldest->newest) ===\n";
        for (int k = 0; k < ring_n; ++k) {
          auto &e = ring[(ring_i + RING - ring_n + k) % RING];
          outFile << dec << e.cycle << " h" << e.hart
                  << " emu_pc=0x" << hex << e.emu_pc
                  << " rtl_pc=0x" << e.rtl_pc
                  << " insn=0x" << e.insn << "\n";
        }
        outFile.flush();
        golden_model.show_state(h);
        cout << "  dumping 200 post-mismatch cycles into " << vcd_path
             << " ..." << endl;
        for (int i = 0; i < 200; ++i) bench.step();
        failed = true;
        break;
      }

      // Register compare: sample one cycle after commit so registersOut*
      // buffers (updated on RegNext(commitFired)) settle. Skip pure PC
      // path when env CHIRON_LOCKSTEP_PC_ONLY=1.
      static const bool pc_only = [] {
        const char *e = std::getenv("CHIRON_LOCKSTEP_PC_ONLY");
        return e && e[0] == '1';
      }();
      if (!pc_only) {
        int r = bench.check_registers(h, golden_model.reg_file(h),
                                      golden_model.get_mstatus(h));
        // Racy shared-memory reconciliation: if the mismatching register is
        // the rd of the load/LR/SC/AMO golden just executed, both outcomes
        // are legal interleavings — adopt the RTL's (see MemOp comment).
        if (r >= 1 && r <= 31 && last_memop[h].valid &&
            r == last_memop[h].rd) {
          const MemOp &op = last_memop[h];
          const uint64_t rtl_rd = bench.read_register(h, r);
          const uint64_t gold_rd = golden_model.reg_file(h)[r];
          golden_model.set_register_with_value((uint8_t)r, rtl_rd, h);
          if (op.kind == 3) {  // SC: reconcile the store's memory effect
            const uint64_t w8 = golden_model.read_mem64(op.addr & ~7ULL);
            if (rtl_rd == 0 && gold_rd != 0) {
              // RTL SC succeeded, golden's failed: apply the store.
              golden_model.write_mem64(op.addr & ~7ULL,
                  splice_word(w8, op.addr, op.width, op.rs2val));
            } else if (rtl_rd != 0 && gold_rd == 0) {
              // RTL SC failed, golden's succeeded: undo the store.
              golden_model.write_mem64(op.addr & ~7ULL, op.oldword);
            }
          } else if (op.kind == 4) {  // AMO: redo RMW from RTL's old value
            const uint64_t neww =
                amo_result(op.funct5, op.width, rtl_rd, op.rs2val);
            const uint64_t w8 = golden_model.read_mem64(op.addr & ~7ULL);
            golden_model.write_mem64(op.addr & ~7ULL,
                splice_word(w8, op.addr, op.width, neww));
          }
          ++racy_fixups;
          // Log the first few occurrences of each distinct (hart, pc) site,
          // then thin. A blanket cap hid the one lock-load divergence behind
          // thousands of identical string-scan fixups.
          static std::map<uint64_t, uint32_t> fixup_seen;
          const uint64_t site = ((uint64_t)h << 62) | rtl_pc;
          const uint32_t n_site = ++fixup_seen[site];
          if (n_site <= 5 || (racy_fixups % 1000) == 0)
            cout << "[racy-fixup #" << dec << racy_fixups << "] h" << h
                 << " pc=0x" << hex << rtl_pc << " kind=" << dec
                 << op.kind << " x" << r << " rtl=0x" << hex << rtl_rd
                 << " golden=0x" << gold_rd << " addr=0x" << op.addr
                 << dec << " site_n=" << n_site << endl;
          r = bench.check_registers(h, golden_model.reg_file(h),
                                    golden_model.get_mstatus(h));
        }
        if (r) {
          dump_mismatch_header("Register mismatch", matched, bench, h);
          cout << "  register x" << dec << r;
          cout << "  RTL=0x" << setfill('0') << setw(16) << hex
               << bench.read_register(h, r) << endl;
          cout << "  golden=0x" << setfill('0') << setw(16) << hex
               << (r == 32 ? golden_model.get_mstatus(h)
                           : golden_model.reg_file(h)[r])
               << endl;
          cout << "  force_take=" << (force_took ? "yes" : "no")
               << " gpr_resync=" << (force_resynced ? "yes" : "no")
               << "  commit pc=0x" << hex << rtl_pc
               << " insn=0x" << golden_model.get_instruction(h) << endl;
          for (int i = 1; i <= 31; ++i) {
            const uint64_t rv = bench.read_register(h, i);
            const uint64_t gv = golden_model.reg_file(h)[i];
            if (rv != gv)
              cout << "    diff x" << dec << i << ": RTL=0x" << hex << rv
                   << " golden=0x" << gv << endl;
          }
          cout << "  (set CHIRON_LOCKSTEP_PC_ONLY=1 to track PC-only)\n";
          outFile << "=== REG MISMATCH ring (oldest->newest) ===\n";
          for (int k = 0; k < ring_n; ++k) {
            auto &e = ring[(ring_i + RING - ring_n + k) % RING];
            outFile << dec << e.cycle << " h" << e.hart
                    << " emu_pc=0x" << hex << e.emu_pc
                    << " rtl_pc=0x" << e.rtl_pc
                    << " insn=0x" << e.insn << "\n";
          }
          outFile.flush();
          golden_model.show_state(h);
          cout << "  dumping 50 cycles of VCD ..." << endl;
          for (int i = 0; i < 50; ++i) bench.step();
          failed = true;
          break;
        }
      }

      matched++;
      matched_per_hart[h]++;
      golden_step_hart(h, bench);
    }
    if (failed) {
      outFile.close();
      return 1;
    }

    if ((matched % 1000000ULL) == 0 && matched > 0) {
      cout << "[lockstep] " << dec << matched << " matched, cycle "
           << (bench.tickcount + bench.dump_tick)
           << " per_hart=[" << matched_per_hart[0] << ","
           << matched_per_hart[1] << "," << matched_per_hart[2] << ","
           << matched_per_hart[3] << "]"
           << " rtl_pc=["
           << "0x" << hex << bench.core_pc(0) << ","
           << "0x" << hex << bench.core_pc(1) << ","
           << "0x" << hex << bench.core_pc(2) << ","
           << "0x" << hex << bench.core_pc(3) << "]"
           << " emu_pc=["
           << "0x" << hex << golden_model.get_pc(0) << ","
           << "0x" << hex << golden_model.get_pc(1) << ","
           << "0x" << hex << golden_model.get_pc(2) << ","
           << "0x" << hex << golden_model.get_pc(3) << "]"
           << endl;
      cout.flush();
    }
  }

  outFile.close();
  printf("Test failed: Time-out!\n");
  printf("Total ticks: %ld\n", (bench.tickcount + bench.dump_tick));
  printf("Matched commits: %llu (h0=%llu h1=%llu h2=%llu h3=%llu)\n",
         (unsigned long long)matched,
         (unsigned long long)matched_per_hart[0],
         (unsigned long long)matched_per_hart[1],
         (unsigned long long)matched_per_hart[2],
         (unsigned long long)matched_per_hart[3]);
  tcflush(0, TCIFLUSH);
  return 1;
}
