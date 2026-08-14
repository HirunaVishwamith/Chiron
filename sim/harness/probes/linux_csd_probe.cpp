// linux_csd_probe — why does hart1 never leave csd_lock_wait?
//
// The quad-core Linux boot livelocks after "Run /init as init process": hart1
// sits at 0x802952c8/cc forever while hart0 keeps taking IPIs. Disassembly of
// the spin site pins down exactly what to look at:
//
//   802952b8: add  a4,a4,a5     # a4 = &csd  (per-cpu call_single_data)
//   802952bc: lw   a5,8(a4)     # csd->node.u_flags
//   802952c0: andi a5,a5,1      # CSD_FLAG_LOCK
//   802952c4: beqz a5,802952dc  # unlocked -> leave the spin
//   802952c8: div  a5,a5,zero   # cpu_relax()
//   802952cc: fence w,unknown
//   802952d0: lw   a5,8(a4)     # reload
//   802952d4: andi a5,a5,1
//   802952d8: bnez a5,802952c8  # spin
//
// So a4 (x14) holds the csd address and the lock bit is bit 0 of the 32-bit
// word at a4+8. Two mutually exclusive explanations for the hang, and one
// measurement separates them:
//
//   flag SET in DRAM   -> the owner never cleared it: hart0 takes the IPI but
//                         never runs the callback (queue not visible to it).
//   flag CLEAR in DRAM -> the clear happened and reached memory, yet hart1
//                         keeps reading it set: hart1 is holding a stale line,
//                         i.e. a load-side coherence bug.
//
// Caveat kept honest: read_dram64() sees DRAM, not dirty lines still parked in
// some L1/L2. "SET in DRAM" therefore means "cleared nowhere visible to DRAM",
// which is why the probe also records whether any hart ever commits inside
// __flush_smp_call_function_queue (where csd_unlock lives) — a store that never
// executed and a store stuck in a cache look different in that trace.
//
// Run:
//   make linux_csd_probe.out
//   CKPT_RESTORE=ckpt/ckpt_001100000000.bin CSD_CYCLES=2000000 \
//     ./build/linux_csd_probe.out
//
// Checkpoint format is byte-identical to linux_ipi_probe.cpp's so the two share
// a checkpoint ladder.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

#define CORE(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__##s

// Kernel symbols (from Multicore_Linux_Image/linux/vmlinux).
static const uint64_t kFlushQueueLo = 0x80294be4ULL;  // __flush_smp_call_function_queue
static const uint64_t kFlushQueueHi = 0x80294e5cULL;  // .. smp_call_on_cpu_callback
static const uint64_t kHandleIPI    = 0x80204968ULL;  // handle_IPI
static const uint64_t kHandleIPIHi  = 0x80204a68ULL;  // +0x100, generous
static const uint64_t kSpinLo       = 0x802952b0ULL;  // csd_lock_wait window
static const uint64_t kSpinHi       = 0x802952e0ULL;
static const uint64_t kPerCpuOffset = 0x807755d0ULL;  // __per_cpu_offset[]
static const uint64_t kCallQueue    = 0x80603500ULL;  // per-cpu call_single_queue

int main(int argc, char **argv) {
  const char *ckpt_restore = getenv("CKPT_RESTORE");
  const char *cy = getenv("CSD_CYCLES");
  const uint64_t RUN = cy ? strtoull(cy, nullptr, 0) : 2000000ULL;
  const char *rp = getenv("CSD_REPORT");
  const uint64_t REPORT = rp ? strtoull(rp, nullptr, 0) : 500000ULL;

  if (!ckpt_restore) {
    std::fprintf(stderr, "linux_csd_probe: set CKPT_RESTORE=<ckpt file>\n");
    return 1;
  }

  uint64_t cyc = 0, msip_writes[4] = {}, last_uart_cyc = 0, uart_bytes = 0;

  simulator bench;
  bench.init_no_image();
  Vsystem *tb = bench.raw();
  {
    VerilatedRestore rs;
    rs.open(ckpt_restore);
    if (!rs.isOpen()) {
      std::fprintf(stderr, "linux_csd_probe: cannot open %s\n", ckpt_restore);
      return 1;
    }
    rs >> cyc;
    rs >> msip_writes[0]; rs >> msip_writes[1];
    rs >> msip_writes[2]; rs >> msip_writes[3];
    rs >> last_uart_cyc;  rs >> uart_bytes;
    rs >> *tb;
    rs.close();
  }
  const uint64_t start_cyc = cyc;

  // Dump-only mode: with the csd address already known, the flag and the queue
  // heads are just DRAM reads — no simulation needed. Restoring every
  // checkpoint in turn and printing one line each brackets the exact 50M window
  // in which the entry was queued and lost, which is far cheaper than guessing
  // a window and simulating tens of millions of cycles into it.
  if (const char *ca = getenv("CSD_ADDR")) {
    uint64_t pcpu[4];
    for (int h = 0; h < 4; h++) pcpu[h] = bench.read_dram64(kPerCpuOffset + 8 * h);
    const uint64_t a = strtoull(ca, nullptr, 0);
    const uint64_t n1 = bench.read_dram64(a + 8);
    std::printf("%14" PRIu64 "  flags=%08x  next=%016" PRIx64
                "  queues=%c%c%c%c  uart=%" PRIu64 "\n",
                cyc, (uint32_t)n1, bench.read_dram64(a),
                bench.read_dram64(kCallQueue + pcpu[0]) ? '1' : '0',
                bench.read_dram64(kCallQueue + pcpu[1]) ? '1' : '0',
                bench.read_dram64(kCallQueue + pcpu[2]) ? '1' : '0',
                bench.read_dram64(kCallQueue + pcpu[3]) ? '1' : '0',
                uart_bytes);
    return 0;
  }

  std::printf("***** restored %s at cycle %" PRIu64 " *****\n", ckpt_restore, cyc);
  std::printf("      running %" PRIu64 " cycles of csd forensics\n\n", RUN);

  // Per-cpu bases straight out of DRAM.
  uint64_t pcpu[4];
  for (int h = 0; h < 4; h++) pcpu[h] = bench.read_dram64(kPerCpuOffset + 8 * h);
  std::printf("__per_cpu_offset = %016" PRIx64 " %016" PRIx64
              " %016" PRIx64 " %016" PRIx64 "\n\n",
              pcpu[0], pcpu[1], pcpu[2], pcpu[3]);

  // Observations we accumulate over the window.
  std::map<uint64_t, uint64_t> pchist[4];
  uint64_t commits[4] = {};
  uint64_t flush_hits[4] = {}, ipi_hits[4] = {}, spin_hits[4] = {};
  uint64_t csd_addr = 0;             // hart1's a4 while spinning
  uint64_t csd_addr_changes = 0;
  std::map<uint32_t, uint64_t> loaded_vals;  // what hart1's reload actually returns
  bool prev_commit[4] = {};
  uint64_t prev_pc_seen[4] = {};

  for (uint64_t i = 0; i < RUN; i++) {
    tb->eval();

    for (int h = 0; h < 4; h++) {
      const bool fired = bench.commit_fired(h);
      // commitFired is a LEVEL. Edge-detecting it collapses every run of
      // back-to-back commits into one sample and drops most of the instruction
      // stream (it made hart1 look like IPC 0.017). Sample every cycle it is
      // high and suppress only an immediate PC repeat.
      if (fired) {
        const uint64_t pc = bench.core_pc(h);
        if (pc == prev_pc_seen[h]) { prev_commit[h] = fired; continue; }
        prev_pc_seen[h] = pc;
        commits[h]++;
        pchist[h][pc]++;
        if (pc >= kFlushQueueLo && pc < kFlushQueueHi) flush_hits[h]++;
        if (pc >= kHandleIPI     && pc < kHandleIPIHi)  ipi_hits[h]++;
        if (pc >= kSpinLo        && pc < kSpinHi)       spin_hits[h]++;

        if (h == 1 && pc >= kSpinLo && pc < kSpinHi) {
          const uint64_t a4 = bench.reg(1, 14);
          if (a4 != csd_addr) { csd_addr_changes++; csd_addr = a4; }
          // a5 right after the reload at 802952d0 (i.e. observed at the commit
          // of the following andi) is the value hart1 actually got back.
          if (pc == 0x802952d4ULL)
            loaded_vals[(uint32_t)bench.reg(1, 15)]++;
        }
      }
      prev_commit[h] = fired;
    }

#define UART(N) if (tb->core##N##OutChar_valid) { putchar((int)tb->core##N##OutChar_byte); fflush(stdout); uart_bytes++; }
    UART(0) UART(1) UART(2) UART(3)
#undef UART

    if ((i + 1) % REPORT == 0) {
      std::fprintf(stderr, "  [+%7" PRIu64 "] commits h0=%" PRIu64 " h1=%" PRIu64
                   " h2=%" PRIu64 " h3=%" PRIu64
                   "  flushq=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
                   i + 1, commits[0], commits[1], commits[2], commits[3],
                   flush_hits[0], flush_hits[1], flush_hits[2], flush_hits[3]);
    }

    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    cyc++;
  }

  std::printf("\n================ CSD FORENSICS ================\n");
  std::printf("window: cycles %" PRIu64 " .. %" PRIu64 "\n\n", start_cyc, cyc);

  for (int h = 0; h < 4; h++) {
    std::printf("hart%d: commits=%-8" PRIu64 " in_spin=%-8" PRIu64
                " in_flush_queue=%-6" PRIu64 " in_handle_IPI=%-6" PRIu64 "\n",
                h, commits[h], spin_hits[h], flush_hits[h], ipi_hits[h]);
    std::vector<std::pair<uint64_t, uint64_t>> top(pchist[h].begin(),
                                                   pchist[h].end());
    std::sort(top.begin(), top.end(),
              [](const std::pair<uint64_t, uint64_t> &a,
                 const std::pair<uint64_t, uint64_t> &b) {
                return a.second > b.second;
              });
    for (size_t k = 0; k < top.size() && k < 8; k++)
      std::printf("        %016" PRIx64 "  x%" PRIu64 "\n",
                  top[k].first, top[k].second);
    std::printf("\n");
  }

  // ---- the decisive read -------------------------------------------------
  if (csd_addr) {
    const uint64_t flag_addr = csd_addr + 8;
    const uint64_t w = bench.read_dram64(flag_addr & ~7ULL);
    const uint32_t flag = (flag_addr & 4) ? (uint32_t)(w >> 32) : (uint32_t)w;
    std::printf("hart1 csd  = %016" PRIx64 "   (a4; changed %" PRIu64 "x in window)\n",
                csd_addr, csd_addr_changes);
    std::printf("flag addr  = %016" PRIx64 "\n", flag_addr);
    std::printf("DRAM value = %08x   -> CSD_FLAG_LOCK = %u\n", flag, flag & 1u);
    std::printf("hart1 read : ");
    for (std::map<uint32_t, uint64_t>::const_iterator it = loaded_vals.begin();
         it != loaded_vals.end(); ++it)
      std::printf("%08x (x%" PRIu64 ") ", it->first, it->second);
    std::printf("\n\n");

    std::printf("VERDICT: ");
    if (loaded_vals.empty()) {
      std::printf("hart1 never completed a reload in this window — inconclusive\n");
    } else if ((flag & 1u) == 0 && loaded_vals.begin()->first & 1u) {
      std::printf("DRAM is CLEAR but hart1 keeps reading it SET.\n"
                  "         -> hart1 holds a stale line: LOAD-SIDE COHERENCE BUG.\n");
    } else if (flag & 1u) {
      std::printf("flag is still SET in DRAM — the owner never cleared it.\n"
                  "         -> the callback never ran; look at the sender/queue side.\n");
    } else {
      std::printf("DRAM clear and hart1 reading clear — it should be leaving; recheck window.\n");
    }
  } else {
    std::printf("hart1 never committed inside the spin window — it is elsewhere.\n");
  }

  // ---- queue state, for the "callback never ran" branch -------------------
  std::printf("\ncall_single_queue heads (llist first node, 0 = empty):\n");
  for (int h = 0; h < 4; h++)
    std::printf("  cpu%d @ %016" PRIx64 " = %016" PRIx64 "\n", h,
                kCallQueue + pcpu[h], bench.read_dram64(kCallQueue + pcpu[h]));

  // ---- the csd itself, decoded -------------------------------------------
  // struct __call_single_node { struct llist_node llist;   // +0  (next)
  //                             unsigned int u_flags;      // +8
  //                             u16 src, dst; };           // +12
  // struct __call_single_data { node; smp_call_func_t func; // +16
  //                             void *info; };              // +24
  // src/dst are the sender and target CPU ids — they name the participants
  // without having to guess the hart->cpu mapping.
  if (csd_addr) {
    const uint64_t n0 = bench.read_dram64(csd_addr + 0);
    const uint64_t n1 = bench.read_dram64(csd_addr + 8);
    const uint64_t fn = bench.read_dram64(csd_addr + 16);
    const uint64_t in = bench.read_dram64(csd_addr + 24);
    std::printf("\nstuck csd @ %016" PRIx64 ":\n", csd_addr);
    std::printf("  node.llist.next = %016" PRIx64 "\n", n0);
    std::printf("  u_flags         = %08x   (LOCK=%u type=0x%02x)\n",
                (uint32_t)n1, (uint32_t)n1 & 1u, (uint32_t)n1 & 0xf0u);
    std::printf("  src             = %u\n", (unsigned)((n1 >> 32) & 0xffff));
    std::printf("  dst             = %u\n", (unsigned)((n1 >> 48) & 0xffff));
    std::printf("  func            = %016" PRIx64 "\n", fn);
    std::printf("  info            = %016" PRIx64 "\n", in);
  }

  // Every CPU's slot in the same csd array — is only one stuck, or all of them?
  if (csd_addr) {
    std::printf("\nsame csd array across all CPUs:\n");
    for (int h = 0; h < 4; h++) {
      const uint64_t a = (csd_addr - pcpu[1]) + pcpu[h];
      const uint64_t n1 = bench.read_dram64(a + 8);
      std::printf("  slot%d @ %016" PRIx64 "  next=%016" PRIx64
                  "  flags=%08x  src=%u dst=%u\n", h, a,
                  bench.read_dram64(a), (uint32_t)n1,
                  (unsigned)((n1 >> 32) & 0xffff), (unsigned)((n1 >> 48) & 0xffff));
    }
  }

  std::printf("\nCLINT msip levels: %u %u %u %u\n",
              bench.msip(0), bench.msip(1), bench.msip(2), bench.msip(3));
  for (int h = 0; h < 4; h++)
    std::printf("  hart%d mcause=%016" PRIx64 "\n", h, bench.mcause(h));

  return 0;
}
