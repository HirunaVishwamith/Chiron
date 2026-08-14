// linux_tramp_probe — why does the I-fetch of the rt_sigreturn trampoline
// return zeros?
//
// Linux nommu has no VDSO, so setup_rt_frame writes a trampoline onto the user
// stack as DATA and returns to it as TEXT:
//
//   0x81b03840:  08b00893   li a7,139     # __NR_rt_sigreturn
//   0x81b03844:  00000073   ecall
//
// The kernel's oops "Code:" dump (a DATA read) showed those bytes present, but
// instruction fetch got 0x00000000 and the core trapped.
//
// WATCH ALL FOUR HARTS. Two earlier traces came back empty because they watched
// the hart named in the Linux oops ("CPU: 1"). Linux's logical CPU numbering is
// NOT the RTL hart index: in this boot `mcause=2` (illegal instruction) appears
// exactly once in the whole log, and it is on **hart0** — which is also the hart
// holding the trampoline line dirty+Unique. Watching one hart guessed from an
// oops is how you get a confident null result.
//
// For the one line, every place a stale copy can hide, on every hart:
//
//   1. Each hart's I-CACHE line — tag/valid/word0. A valid line whose tag
//      matches and whose word0 is 0x00000000 IS the bug, and the cycle it was
//      filled dates it.
//   2. Each hart's I-cache refill as it lands off the bus, with the data.
//   3. Each hart's D-cache copy (tag/valid/dirty/shared). Two harts holding it
//      V1/M1/S0 at once is a known chiron defect (dual-Unique violation).
//   4. Each hart's walker + normal writeback staging slots WITH THE DATA WORD.
//      This is what separates "the fence.i walker never wrote the line back"
//      from "it wrote it back and something downstream lost it": if the buffer
//      holds line 0x81b03840 carrying 0x08b00893, the walker did its job.
//   5. Every hart's fence.i walker sweep, so the flush that was supposed to
//      publish the line is placed on the timeline against the fill.
//
// Geometry (Icache/ICache.scala, configuration.scala: offset 4, line 6):
//   line = (addr >> 6) & 0x3f, tag = addr >> 12, word = (addr >> 2) & 0xf
// D-cache (cacheLookupUnit.scala): 128 sets x 4 ways, tagSection 23 bits.
//   set = (addr >> 6) & 0x7f, tag = addr >> 13.
//
// Run:
//   CKPT_RESTORE=ckpt/ckpt_001200000000.bin TR_ADDR=0x81b03840 \
//     TR_FROM=1200000000 TR_TO=1240600000 ./build/linux_tramp_probe.out

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

static const int kTagSize    = 19;   // D-cache
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
  uint32_t tag, valid, dirty, shared;
  bool operator!=(const WayState &o) const {
    return tag != o.tag || valid != o.valid || dirty != o.dirty ||
           shared != o.shared;
  }
};

int main(int argc, char **argv) {
  const char *ckpt = getenv("CKPT_RESTORE");
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }
  const uint64_t ADDR = getenv("TR_ADDR")
      ? strtoull(getenv("TR_ADDR"), nullptr, 0) : 0x81b03840ULL;
  const uint64_t FROM = getenv("TR_FROM")
      ? strtoull(getenv("TR_FROM"), nullptr, 0) : 1200000000ULL;
  const uint64_t TO = getenv("TR_TO")
      ? strtoull(getenv("TR_TO"), nullptr, 0) : 1240600000ULL;

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

  const uint64_t line  = ADDR & ~63ULL;
  const uint32_t dset  = (uint32_t)((ADDR >> 6) & 0x7f);
  const uint32_t dtag  = (uint32_t)(ADDR >> 13);
  const uint32_t iline = (uint32_t)((ADDR >> 6) & 0x3f);
  const uint32_t itag  = (uint32_t)(ADDR >> 12);
  const uint32_t iword = (uint32_t)((ADDR >> 2) & 0xf);
  const uint32_t lword = (uint32_t)((ADDR >> 2) & 0xf);  // word within the line

  std::printf("restored %s at %" PRIu64 "\n", ckpt, cyc);
  std::printf("line %016" PRIx64 "   (ALL FOUR HARTS watched)\n", line);
  std::printf("  D-cache: set=%u tag=%05x\n", dset, dtag);
  std::printf("  I-cache: line=%u tag=%05x word=%u\n", iline, itag, iword);
  std::printf("window %" PRIu64 " .. %" PRIu64 "\n\n", FROM, TO);
  std::fflush(stdout);

  // Verilator flattens the hierarchy, so each core's nets are distinct members
  // and cannot be indexed at runtime — hence the per-hart switch macros.
#define DC(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__memAccess__DOT__cacheLookup__DOT__##s
#define DPICK(h, s) ((h) == 1 ? (uint64_t)DC(1, s) : (h) == 2 ? (uint64_t)DC(2, s) \
                   : (h) == 3 ? (uint64_t)DC(3, s) : (uint64_t)DC(0, s))
#define DROW(h, row) ((h) == 1 ? DC(1, tagBRAM__DOT__mem)[row] \
                    : (h) == 2 ? DC(2, tagBRAM__DOT__mem)[row] \
                    : (h) == 3 ? DC(3, tagBRAM__DOT__mem)[row] \
                               : DC(0, tagBRAM__DOT__mem)[row])
#define DDATA(h, s) ((h) == 1 ? (const uint32_t *)DC(1, s) \
                   : (h) == 2 ? (const uint32_t *)DC(2, s) \
                   : (h) == 3 ? (const uint32_t *)DC(3, s) \
                              : (const uint32_t *)DC(0, s))
#define IC(N, s) tb->system__DOT__chiron__DOT__core##N##__DOT__icache__DOT__##s
#define IPICK(h, s) ((h) == 1 ? (uint64_t)IC(1, s) : (h) == 2 ? (uint64_t)IC(2, s) \
                   : (h) == 3 ? (uint64_t)IC(3, s) : (uint64_t)IC(0, s))
#define ITAGS(h) ((h) == 1 ? IC(1, cache__DOT__tags) : (h) == 2 ? IC(2, cache__DOT__tags) \
                : (h) == 3 ? IC(3, cache__DOT__tags) : IC(0, cache__DOT__tags))
#define IVALID(h) ((h) == 1 ? IC(1, cache__DOT__validBits) : (h) == 2 ? IC(2, cache__DOT__validBits) \
                 : (h) == 3 ? IC(3, cache__DOT__validBits) : IC(0, cache__DOT__validBits))
#define ILINE(h) ((h) == 1 ? IC(1, cache__DOT__cache)[iline] : (h) == 2 ? IC(2, cache__DOT__cache)[iline] \
                : (h) == 3 ? IC(3, cache__DOT__cache)[iline] : IC(0, cache__DOT__cache)[iline])
#define IFILL(h) ((h) == 1 ? (const uint32_t *)IC(1, cacheFill_block) \
                : (h) == 2 ? (const uint32_t *)IC(2, cacheFill_block) \
                : (h) == 3 ? (const uint32_t *)IC(3, cacheFill_block) \
                           : (const uint32_t *)IC(0, cacheFill_block))

  WayState dprev[4][kNway];
  std::memset(dprev, 0xff, sizeof dprev);
  int      walkerActive[4] = {-1, -1, -1, -1};
  uint32_t iprev_tag[4], iprev_word[4];
  int      iprev_valid[4], iprev_fill[4], prev_wwb[4], prev_wb[4];
  for (int h = 0; h < 4; h++) {
    iprev_tag[h] = 0xffffffff; iprev_word[h] = 0xffffffff;
    iprev_valid[h] = -1; iprev_fill[h] = -1; prev_wwb[h] = -1; prev_wb[h] = -1;
  }

  for (;;) {
    tb->eval();

    if (cyc >= FROM && cyc < TO) {
      for (int h = 0; h < 4; h++) {
        // ---- 1. I-cache line -------------------------------------------------
        const uint32_t it = ITAGS(h)[iline];
        const uint32_t iv = IVALID(h)[iline];
        const uint32_t iw = ILINE(h)[iword];
        if ((int)iv != iprev_valid[h] || it != iprev_tag[h] || iw != iprev_word[h]) {
          // The I-cache is only 64 x 64 B, so this line is reused constantly by
          // unrelated kernel text; narrate only when the watched tag is
          // involved, entering or leaving, or the log is unreadable.
          const bool hit = (iv && it == itag);
          const bool was = (iprev_valid[h] == 1 && iprev_tag[h] == itag);
          if (hit || was)
            std::printf("%12" PRIu64 " ICACHE h%d line%2u: V%u tag=%05x word%u=%08x%s\n",
                        cyc, h, iline, iv, it, iword, iw,
                        hit ? (iw == 0 ? "   <== WATCHED LINE, ZEROS"
                                       : "   <== WATCHED LINE, real code") : "");
          iprev_valid[h] = (int)iv; iprev_tag[h] = it; iprev_word[h] = iw;
        }

        // ---- 2. refill in flight, watched line only ---------------------------
        const int fillv = (int)IPICK(h, cacheFill_valid);
        if (fillv != iprev_fill[h]) {
          if (fillv) {
            const uint64_t fa = IPICK(h, results_0_address);
            if ((fa >> 6) == (ADDR >> 6)) {
              const uint32_t *blk = IFILL(h);
              std::printf("%12" PRIu64 "   ifill h%d LANDS for %08" PRIx64
                          " word0=%08x word1=%08x   <== WATCHED LINE\n",
                          cyc, h, fa, blk[0], blk[1]);
            }
          }
          iprev_fill[h] = fillv;
        }

        // ---- 3. D-cache copy --------------------------------------------------
        const uint32_t *row = (const uint32_t *)DROW(h, dset);
        WayState now[kNway];
        bool changed = false, holds = false, held_before = false;
        for (int w = 0; w < kNway; w++) {
          const int b = w * kTagSection;
          now[w].tag    = wbits(row, b, kTagSize);
          now[w].valid  = wbits(row, b + kTagSize + 0, 1);
          now[w].dirty  = wbits(row, b + kTagSize + 1, 1);
          now[w].shared = wbits(row, b + kTagSize + 2, 1);
          if (now[w] != dprev[h][w]) changed = true;
          if (now[w].tag == dtag && now[w].valid) holds = true;
          if (dprev[h][w].tag == dtag && dprev[h][w].valid) held_before = true;
        }
        if (changed && (holds || held_before)) {
          std::printf("%12" PRIu64 " DCACHE h%d set%3u:", cyc, h, dset);
          for (int w = 0; w < kNway; w++)
            std::printf("  w%d[tag=%05x V%u M%u S%u]%s", w, now[w].tag,
                        now[w].valid, now[w].dirty, now[w].shared,
                        (now[w].tag == dtag && now[w].valid) ? "*" : "");
          std::printf("\n");
        }
        if (changed) std::memcpy(dprev[h], now, sizeof now);

        // ---- 4. writeback staging slots, WITH the data ------------------------
        // The data word is the point: it separates "the walker never wrote this
        // line back" from "it did, and the value was right".
        const int wwb = (int)DPICK(h, walkerWriteBackBuffer_valid) &&
            ((DPICK(h, walkerWriteBackBuffer_address) & ~63ULL) == (line & 0xffffffffULL));
        if (wwb != prev_wwb[h]) {
          if (wwb) {
            const uint32_t *d = DDATA(h, walkerWriteBackBuffer_data);
            std::printf("%12" PRIu64 "   walkerWB h%d HOLDS watched line "
                        "retain=%" PRIu64 " word%u=%08x word0=%08x\n",
                        cyc, h, DPICK(h, walkerWriteBackBuffer_retain),
                        lword, d[lword], d[0]);
          }
          prev_wwb[h] = wwb;
        }
        const int wb = (int)DPICK(h, writeBackBuffer_valid) &&
            ((DPICK(h, writeBackBuffer_address) & ~63ULL) == (line & 0xffffffffULL));
        if (wb != prev_wb[h]) {
          if (wb) {
            const uint32_t *d = DDATA(h, writeBackBuffer_data);
            std::printf("%12" PRIu64 "   evictWB h%d HOLDS watched line "
                        "word%u=%08x word0=%08x\n",
                        cyc, h, lword, d[lword], d[0]);
          }
          prev_wb[h] = wb;
        }

        // ---- 5. fence.i walker sweeps ----------------------------------------
        const int act = DPICK(h, flushState) != 0;
        if (act != walkerActive[h]) {
          if (walkerActive[h] >= 0)
            std::printf("%12" PRIu64 "   walker h%d %s\n", cyc, h,
                        act ? "SWEEP START" : "sweep end");
          walkerActive[h] = act;
        }
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
