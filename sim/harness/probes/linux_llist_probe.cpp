// linux_llist_probe — a push/pop ledger for Linux's cross-call queue.
//
// The /init hang leaves a csd locked with every call_single_queue empty (see
// linux_csd_probe). An entry that is locked but on no queue was either never
// published or was erased, and the directed benchmarks did not reproduce
// either: mt-fencei (fence.i then release store) PASSES both ways, and
// mt-llist (cmpxchg racing amoswap on one word) PASSES. So stop guessing at
// primitives and read the ledger off the failing run itself.
//
// Both queue operations have exact, unambiguous PCs in this vmlinux:
//
//   llist_add_batch:
//     803fc6d0  lr.d      a4,(a2)
//     803fc6d8  sc.d.rl   a3,a0,(a2)     a2=&head->first  a0=node
//     803fc6dc  bnez      a3,803fc6d0    a3==0 => the push COMMITTED
//
//   __flush_smp_call_function_queue:
//     80294c3c  amoswap.d.aqrl a0,a4,(a5)   a5=&head->first, a4=0
//     80294c40  (next)                      a0 = the chain taken
//
// Sampling the register file at the commit of the instruction AFTER each
// atomic gives, per event: which queue, which node, and whether it worked.
// Conservation then decides the case outright — a push that commits with no
// later pop returning a non-NULL chain for that queue is an entry the hardware
// dropped.
//
// Run (restore a checkpoint from before the hang; uart dies around 840M):
//   make linux_llist_probe.out
//   CKPT_RESTORE=ckpt/ckpt_000800000000.bin LLIST_CYCLES=120000000 \
//     ./build/linux_llist_probe.out
//
// It stops early once a hart has sat in csd_lock_wait continuously for
// SPIN_HANG cycles, and dumps the last events leading up to that.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
#include <algorithm>
#include <string>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

#define CORE(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__##s

// Watchpoints (Multicore_Linux_Image/linux/vmlinux).
static const uint64_t kPushDone  = 0x803fc6dcULL;  // after sc.d.rl
static const uint64_t kPopDone   = 0x80294c40ULL;  // after amoswap.d.aqrl
static const uint64_t kFlushEnt  = 0x80294be4ULL;  // __flush_smp_call_function_queue
static const uint64_t kIpiSingle = 0x80233da8ULL;  // send_call_function_single_ipi
static const uint64_t kIpiMask   = 0x80204c84ULL;  // arch_send_call_function_ipi_mask
static const uint64_t kSpinLo    = 0x802952b0ULL;  // csd_lock_wait window
static const uint64_t kSpinHi    = 0x802952e0ULL;

struct Event {
  uint64_t cyc;
  int      hart;
  char     kind;      // P=push S=pop F=flush I=ipi_single M=ipi_mask
  uint64_t queue;     // &head->first
  uint64_t node;      // pushed node / drained chain
  uint64_t ok;        // push: sc result (0 = committed)
};

int main(int argc, char **argv) {
  const char *ckpt_restore = getenv("CKPT_RESTORE");
  const char *cy = getenv("LLIST_CYCLES");
  const uint64_t RUN = cy ? strtoull(cy, nullptr, 0) : 120000000ULL;
  const char *sh = getenv("SPIN_HANG");
  const uint64_t SPIN_HANG = sh ? strtoull(sh, nullptr, 0) : 3000000ULL;
  const char *kp = getenv("LLIST_KEEP");
  const size_t KEEP = kp ? (size_t)strtoul(kp, nullptr, 0) : 240u;

  if (!ckpt_restore) {
    std::fprintf(stderr, "linux_llist_probe: set CKPT_RESTORE=<ckpt file>\n");
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
      std::fprintf(stderr, "linux_llist_probe: cannot open %s\n", ckpt_restore);
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
  std::printf("***** restored %s at cycle %" PRIu64 " *****\n", ckpt_restore, cyc);
  std::printf("      ledger: sc.d@%" PRIx64 " (push)  amoswap@%" PRIx64 " (pop)\n",
              kPushDone, kPopDone);
  std::printf("      stopping after a hart spins %" PRIu64 " cycles in csd_lock_wait\n\n",
              SPIN_HANG);
  std::fflush(stdout);

  std::deque<Event> ring;
  // Outstanding pushes per queue: a push adds, a non-NULL pop clears the queue.
  std::map<uint64_t, uint64_t> pushes, pops, pops_null, live;
  std::map<uint64_t, uint64_t> pchist[4];
  uint64_t spin_since[4] = {0, 0, 0, 0};
  bool     in_spin[4] = {};
  bool     prev_commit[4] = {};
  uint64_t prev_pc_seen[4] = {};
  uint64_t total_push = 0, total_pop = 0;
  const char *stop_reason = "cycle budget exhausted";

  const char *tf = getenv("TRACE_FROM");
  const uint64_t TRACE_FROM = tf ? strtoull(tf, nullptr, 0) : ~0ULL;
  const char *tt = getenv("TRACE_TO");
  const uint64_t TRACE_TO = tt ? strtoull(tt, nullptr, 0) : 0ULL;
  const char *th = getenv("TRACE_HART");
  const int TRACE_HART = th ? atoi(th) : -1;

  const char *wa = getenv("CSD_WATCH");
  const uint64_t watch_addr = wa ? strtoull(wa, nullptr, 0) : 0ULL;
  const char *wt = getenv("WATCH_TAIL");
  const uint64_t WATCH_TAIL = wt ? strtoull(wt, nullptr, 0) : 2000000ULL;
  uint32_t watch_prev = watch_addr
      ? (uint32_t)bench.read_dram64(watch_addr + 8) : 0u;
  bool     watch_armed = false;
  uint64_t watch_at = 0;
  if (watch_addr)
    std::printf("      watching csd %016" PRIx64 " (flags now %08x)\n\n",
                watch_addr, watch_prev);

  for (uint64_t i = 0; i < RUN; i++) {
    tb->eval();

    for (int h = 0; h < 4; h++) {
      const bool fired = bench.commit_fired(h);
      // commitFired is a LEVEL, not a pulse. Edge-detecting it collapses every
      // run of back-to-back commits into a single sample, which silently drops
      // most of the instruction stream — a tight callee like llist_add_batch
      // disappears completely and its push is never seen. Sample every cycle
      // the signal is high, and suppress only an immediate PC repeat (a commit
      // port held on the same instruction across a stall).
      if (fired) {
        const uint64_t pc = bench.core_pc(h);
        if (pc == prev_pc_seen[h]) { prev_commit[h] = fired; continue; }
        prev_pc_seen[h] = pc;
        if (watch_armed) pchist[h][pc]++;   // where each hart goes after the lock
        // Full PC trace over a narrow window. The failure is a ~30k-cycle
        // stretch right after a POP, so dumping every committed PC there shows
        // directly whether the servicing hart reaches the callback and the
        // csd_unlock store, or never gets that far.
        if (cyc >= TRACE_FROM && cyc < TRACE_TO && (TRACE_HART < 0 || TRACE_HART == h))
          std::printf("T %" PRIu64 " h%d %012" PRIx64 "\n", cyc, h, pc);

        if (pc == kPushDone) {
          Event e{cyc, h, 'P', bench.reg(h, 12), bench.reg(h, 10),
                  bench.reg(h, 13)};
          ring.push_back(e);
          if (e.ok == 0) { pushes[e.queue]++; live[e.queue]++; total_push++; }
        } else if (pc == kPopDone) {
          Event e{cyc, h, 'S', bench.reg(h, 15), bench.reg(h, 10), 0};
          ring.push_back(e);
          if (e.node) { pops[e.queue]++; live[e.queue] = 0; total_pop++; }
          else        { pops_null[e.queue]++; }
        } else if (pc == kFlushEnt) {
          ring.push_back(Event{cyc, h, 'F', 0, 0, 0});
        } else if (pc == kIpiSingle) {
          ring.push_back(Event{cyc, h, 'I', 0, bench.reg(h, 10), 0});
        } else if (pc == kIpiMask) {
          ring.push_back(Event{cyc, h, 'M', 0, bench.reg(h, 10), 0});
        }
        while (ring.size() > KEEP) ring.pop_front();

        // Continuous residency in the spin window = the hang forming.
        const bool spinning = (pc >= kSpinLo && pc < kSpinHi);
        if (spinning) {
          if (!in_spin[h]) { in_spin[h] = true; spin_since[h] = cyc; }
        } else {
          in_spin[h] = false;
        }
      }
      prev_commit[h] = fired;
    }

#define UART(N) if (tb->core##N##OutChar_valid) { putchar((int)tb->core##N##OutChar_byte); fflush(stdout); uart_bytes++; last_uart_cyc = cyc; }
    UART(0) UART(1) UART(2) UART(3)
#undef UART

    // Flag watchpoint. The ledger showed the lock bit being set with NO push
    // following it, and llist_add_batch's retry path (lr.d -> bne -> retry)
    // never reaches the push watchpoint — so a hart wedged in that cmpxchg loop
    // is invisible above. Watching the csd word directly catches the transition
    // itself, and freezing every hart's PC at that cycle says who set it and
    // where they all went next.
    if (watch_addr) {
      const uint64_t w = bench.read_dram64(watch_addr + 8);
      const uint32_t f = (uint32_t)w;
      if (f != watch_prev) {
        std::printf("\n*** [cyc %" PRIu64 "] csd %016" PRIx64 " flags %08x -> %08x\n",
                    cyc, watch_addr, watch_prev, f);
        for (int h = 0; h < 4; h++)
          std::printf("      hart%d pc=%016" PRIx64 "\n", h, bench.core_pc(h));
        std::fflush(stdout);
        watch_prev = f;
        // Arm on every transition INTO the locked state and disarm the moment
        // it clears. csd_lock/csd_unlock cycle constantly during healthy
        // operation, so arming once on the first lock catches a benign one;
        // only a lock that survives WATCH_TAIL is the terminal one.
        if (f & 1u) { watch_armed = true; watch_at = cyc;
                      for (int h = 0; h < 4; h++) pchist[h].clear(); }
        else        { watch_armed = false; }
      }
      // A lock that has not cleared for a full tail is the one that hangs.
      if (watch_armed && cyc - watch_at > WATCH_TAIL) {
        stop_reason = "csd lock observed + tail elapsed";
        break;
      }
    }

    bool hung = false;
    for (int h = 0; h < 4; h++)
      if (in_spin[h] && cyc - spin_since[h] > SPIN_HANG) {
        std::printf("\n*** hart%d has been in csd_lock_wait since cycle %"
                    PRIu64 " (%" PRIu64 " cycles) ***\n",
                    h, spin_since[h], cyc - spin_since[h]);
        hung = true;
      }
    if (hung) { stop_reason = "csd_lock_wait hang detected"; break; }

    if ((i + 1) % 10000000 == 0) {
      std::fprintf(stderr, "  [cyc %" PRIu64 "] push=%" PRIu64 " pop=%" PRIu64
                   " uart=%" PRIu64 " (quiet %" PRIu64 ")\n",
                   cyc, total_push, total_pop, uart_bytes, cyc - last_uart_cyc);
    }

    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    cyc++;
  }

  std::printf("\n================ QUEUE LEDGER ================\n");
  std::printf("window %" PRIu64 " .. %" PRIu64 "   stop: %s\n",
              start_cyc, cyc, stop_reason);
  std::printf("total committed pushes = %" PRIu64 "   non-empty pops = %" PRIu64 "\n\n",
              total_push, total_pop);

  std::printf("per queue:  pushes  pops  null-pops  LIVE(unclaimed)\n");
  bool leak = false;
  for (std::map<uint64_t, uint64_t>::const_iterator it = pushes.begin();
       it != pushes.end(); ++it) {
    const uint64_t q = it->first;
    const uint64_t l = live.count(q) ? live.at(q) : 0;
    std::printf("  %016" PRIx64 "  %6" PRIu64 " %6" PRIu64 " %8" PRIu64
                " %8" PRIu64 "%s\n", q, it->second,
                pops.count(q) ? pops.at(q) : 0,
                pops_null.count(q) ? pops_null.at(q) : 0, l,
                l ? "   <== PUSHED BUT NEVER DRAINED" : "");
    if (l) leak = true;
  }

  std::printf("\nlast %zu events (P=push S=pop F=flush I=ipi M=ipi_mask):\n",
              ring.size());
  for (std::deque<Event>::const_iterator it = ring.begin(); it != ring.end(); ++it) {
    const Event &e = *it;
    switch (e.kind) {
      case 'P':
        std::printf("  %12" PRIu64 " h%d PUSH  queue=%016" PRIx64 " node=%016" PRIx64
                    " sc=%" PRIu64 " %s\n", e.cyc, e.hart, e.queue, e.node, e.ok,
                    e.ok == 0 ? "COMMITTED" : "(retry)");
        break;
      case 'S':
        std::printf("  %12" PRIu64 " h%d POP   queue=%016" PRIx64 " chain=%016" PRIx64
                    " %s\n", e.cyc, e.hart, e.queue, e.node,
                    e.node ? "" : "(empty)");
        break;
      case 'F':
        std::printf("  %12" PRIu64 " h%d FLUSH enter\n", e.cyc, e.hart); break;
      case 'I':
        std::printf("  %12" PRIu64 " h%d IPI   -> cpu %" PRIu64 "\n",
                    e.cyc, e.hart, e.node); break;
      case 'M':
        std::printf("  %12" PRIu64 " h%d IPI_MASK\n", e.cyc, e.hart); break;
      default: break;
    }
  }

  std::printf("\nwhere each hart sat AFTER the lock was observed:\n");
  for (int h = 0; h < 4; h++) {
    std::vector<std::pair<uint64_t,uint64_t> > top(pchist[h].begin(), pchist[h].end());
    std::sort(top.begin(), top.end(),
              [](const std::pair<uint64_t,uint64_t>&a, const std::pair<uint64_t,uint64_t>&b){
                return a.second > b.second; });
    std::printf("  hart%d:", h);
    for (size_t k = 0; k < top.size() && k < 6; k++)
      std::printf("  %012" PRIx64 " x%" PRIu64, top[k].first, top[k].second);
    std::printf("\n");
  }

  std::printf("\nVERDICT: ");
  if (leak)
    std::printf("a push COMMITTED (sc.d returned success) and that queue was\n"
                "         never drained non-empty afterwards — the hardware dropped\n"
                "         the publishing store.\n");
  else if (total_push == 0)
    std::printf("no push was observed in this window — widen it or start earlier.\n");
  else
    std::printf("every committed push was drained; the entry is lost AFTER the\n"
                "         pop, i.e. in the callback/csd_unlock path, not the queue.\n");
  return 0;
}
