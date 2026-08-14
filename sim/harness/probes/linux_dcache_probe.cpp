// linux_dcache_probe — watch ONE D-cache line through the lost-store window.
//
// The /init hang is a committed store that never becomes visible:
//
//   871231851 h0 802054f4  fence.i          (walker sweep, 22,543 cycles)
//   871254695 h0 80294d64  lw   a5,8(s3)    flags still locked
//   871254879 h0 80294d28  fence rw,w
//   871254973 h0 80294d2c  sw   zero,8(s3)  csd_unlock(): COMMITS, then LOST
//
// hart2 loses the same store 501 cycles earlier on a DIFFERENT line; hart3,
// doing identical work ~9,000 cycles later and alone, succeeds. Four C-level
// repros (mt-fencei in two shapes, mt-llist, mt-crosscall) all PASS, so the
// mechanism is not reachable by guessing programs from outside — read the
// cache state directly instead.
//
// Tag layout (cacheLookupUnit.scala:142): PLRU | Shared | Modified | Valid | tag
//   tagSize    = addrWidth - dataAddrWidth - log2(lineSize) = 32 - 7 - 6 = 19
//   tagSection = 4 + tagSize = 23,  nway = 4,  128 sets,  64 B lines
//   tagBRAM.mem is WData[128][3] — 92 bits per set.
//
// Line 0x87dd6b80 -> set = (addr >> 6) & 0x7f, tag = addr >> 13.
//
// Prints every change to that set's four ways, plus the walker FSM
// (flushState/flushSet/flushWay/poison) and both writeback staging slots
// whenever they carry the watched line. The question it answers: after the
// walker writes the line back, what does the tag say, and what does the store
// at 871254973 actually do to it?
//
// Run:
//   make linux_dcache_probe.out
//   CKPT_RESTORE=ckpt/ckpt_000850000000.bin DC_HART=0 DC_ADDR=0x87dd6b80 \
//     DC_FROM=871225000 DC_TO=871262000 ./build/linux_dcache_probe.out

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

static const int kTagSize    = 19;
static const int kTagSection = 23;
static const int kNway       = 4;

// Extract [lo +: len] from a little-endian array of 32-bit words.
static inline uint32_t wbits(const uint32_t *w, int lo, int len) {
  const int wi = lo >> 5, off = lo & 31;
  uint64_t v = (uint64_t)w[wi] >> off;
  if (off + len > 32) v |= (uint64_t)w[wi + 1] << (32 - off);
  return (uint32_t)(v & ((len >= 32) ? 0xffffffffu : ((1u << len) - 1u)));
}

struct WayState {
  uint32_t tag, valid, dirty, shared, plru;
  bool operator!=(const WayState &o) const {
    return tag != o.tag || valid != o.valid || dirty != o.dirty ||
           shared != o.shared || plru != o.plru;
  }
};

int main(int argc, char **argv) {
  const char *ckpt = getenv("CKPT_RESTORE");
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }
  const int      HART = getenv("DC_HART") ? atoi(getenv("DC_HART")) : 0;
  const uint64_t ADDR = getenv("DC_ADDR")
      ? strtoull(getenv("DC_ADDR"), nullptr, 0) : 0x87dd6b80ULL;
  const uint64_t FROM = getenv("DC_FROM")
      ? strtoull(getenv("DC_FROM"), nullptr, 0) : 871225000ULL;
  const uint64_t TO = getenv("DC_TO")
      ? strtoull(getenv("DC_TO"), nullptr, 0) : 871262000ULL;

  uint64_t cyc = 0, msip[4] = {}, last_uart = 0, uart_bytes = 0;
  simulator bench;
  bench.init_no_image();
  Vsystem *tb = bench.raw();
  {
    VerilatedRestore rs;
    rs.open(ckpt);
    if (!rs.isOpen()) { std::fprintf(stderr, "cannot open %s\n", ckpt); return 1; }
    rs >> cyc; rs >> msip[0]; rs >> msip[1]; rs >> msip[2]; rs >> msip[3];
    rs >> last_uart; rs >> uart_bytes; rs >> *tb; rs.close();
  }

  const uint32_t set = (uint32_t)((ADDR >> 6) & 0x7f);
  const uint32_t tag = (uint32_t)(ADDR >> 13);
  const uint64_t line = ADDR & ~63ULL;
  std::printf("restored %s at %" PRIu64 "\n", ckpt, cyc);
  std::printf("watching hart%d line %016" PRIx64 "  set=%u tag=%05x\n",
              HART, line, set, tag);
  std::printf("window %" PRIu64 " .. %" PRIu64 "\n\n", FROM, TO);
  std::fflush(stdout);

  // Per-hart signal access. Verilator flattens the hierarchy, so each core's
  // nets are distinct members and there is no way to index them at runtime.
#define DC(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__memAccess__DOT__cacheLookup__DOT__##s
#define PICK(s) (HART == 1 ? (uint64_t)DC(1, s) : HART == 2 ? (uint64_t)DC(2, s) \
                : HART == 3 ? (uint64_t)DC(3, s) : (uint64_t)DC(0, s))
#define MEMROW(row) (HART == 1 ? DC(1, tagBRAM__DOT__mem)[row] \
                   : HART == 2 ? DC(2, tagBRAM__DOT__mem)[row] \
                   : HART == 3 ? DC(3, tagBRAM__DOT__mem)[row] \
                               : DC(0, tagBRAM__DOT__mem)[row])

  WayState prev[kNway];
  std::memset(prev, 0xff, sizeof prev);
  uint64_t prev_flush = ~0ULL;
  int      prev_wwb = -1, prev_wb = -1;

  for (;;) {
    tb->eval();

    if (cyc >= FROM && cyc < TO) {
      const uint32_t *row = (const uint32_t *)MEMROW(set);
      WayState now[kNway];
      bool changed = false;
      for (int w = 0; w < kNway; w++) {
        const int b = w * kTagSection;
        now[w].tag    = wbits(row, b, kTagSize);
        now[w].valid  = wbits(row, b + kTagSize + 0, 1);
        now[w].dirty  = wbits(row, b + kTagSize + 1, 1);
        now[w].shared = wbits(row, b + kTagSize + 2, 1);
        now[w].plru   = wbits(row, b + kTagSize + 3, 1);
        if (now[w] != prev[w]) changed = true;
      }
      if (changed) {
        std::printf("%12" PRIu64 " SET%3u:", cyc, set);
        for (int w = 0; w < kNway; w++)
          std::printf("  w%d[tag=%05x V%u M%u S%u L%u]%s", w, now[w].tag,
                      now[w].valid, now[w].dirty, now[w].shared, now[w].plru,
                      (now[w].tag == tag && now[w].valid) ? "*" : "");
        std::printf("\n");
        for (int w = 0; w < kNway; w++) prev[w] = now[w];
      }

      // Walker FSM: 0=idle 1=read 2=capture 3=emit 4=drain (cacheLookupUnit).
      const uint64_t fs = (PICK(flushState) << 16) | (PICK(flushSet) << 4) |
                          (PICK(flushWay) << 2) | PICK(flushSetPoison);
      if (fs != prev_flush) {
        const uint64_t st = PICK(flushState);
        // Only narrate the sweep as it passes the watched set, plus every
        // state change, so the log stays readable across a 22k-cycle sweep.
        if (PICK(flushSet) == set || st != (prev_flush >> 16))
          std::printf("%12" PRIu64 "   walker state=%" PRIu64 " set=%" PRIu64
                      " way=%" PRIu64 " poison=%" PRIu64 "%s\n", cyc, st,
                      PICK(flushSet), PICK(flushWay), PICK(flushSetPoison),
                      PICK(flushSet) == set ? "   <== watched set" : "");
        prev_flush = fs;
      }

      // Writeback staging slots carrying the watched line.
      const int wwb = (int)PICK(walkerWriteBackBuffer_valid) &&
                      ((PICK(walkerWriteBackBuffer_address) & ~63ULL) == (line & 0xffffffffULL));
      if (wwb != prev_wwb) {
        std::printf("%12" PRIu64 "   walkerWriteBackBuffer %s line %08" PRIx64 "\n",
                    cyc, wwb ? "HOLDS" : "released",
                    PICK(walkerWriteBackBuffer_address) & ~63ULL);
        prev_wwb = wwb;
      }
      const int wb = (int)PICK(writeBackBuffer_valid) &&
                     ((PICK(writeBackBuffer_address) & ~63ULL) == (line & 0xffffffffULL));
      if (wb != prev_wb) {
        std::printf("%12" PRIu64 "   writeBackBuffer %s line %08" PRIx64 "\n",
                    cyc, wb ? "HOLDS" : "released",
                    PICK(writeBackBuffer_address) & ~63ULL);
        prev_wb = wb;
      }
    }

    if (cyc >= TO) break;
    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    cyc++;
    if (cyc % 5000000 == 0) {
      std::fprintf(stderr, "  [cyc %" PRIu64 "]\n", cyc);
      std::fflush(stderr);
    }
  }
  std::printf("\ndone at %" PRIu64 "\n", cyc);
  return 0;
}
