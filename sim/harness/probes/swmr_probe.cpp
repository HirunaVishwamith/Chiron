// swmr_probe — continuous coherence-invariant checker over all four L1 D-caches.
//
// This is oracle Layer 1 of docs/research/03-the-idea.md, in prototype form.
//
// Why it exists. Commit-level lockstep (DiffTest, Dromajo, our own) observes at
// the architectural commit boundary, so a coherence fault is only visible once
// it has propagated into a committed architectural value — if it ever does. The
// dual-Unique bug (docs/research/04, CO-1) was found by hand-dumping tag arrays
// and seeing the SAME LINE held V1 M1 S0 by two harts at once; it manifested as
// a Linux /init hang roughly 10^8 cycles later, and five directed reproducers
// passed on both the buggy and the fixed RTL. This probe turns that one-off
// manual dump into a continuous, automatic, cycle-level invariant.
//
// Invariants checked, per (set, tag), every cycle:
//
//   SWMR        single-writer/multiple-reader: at most one hart may hold a line
//               valid-and-not-shared (i.e. Unique). Two owners is the literal
//               dual-Unique signature.
//   MULTI-DIRTY at most one hart may hold a line valid-and-dirty. Strictly
//               implied by SWMR when the shared bit is maintained correctly,
//               and reported separately precisely so a shared-bit bug is
//               distinguishable from an ownership bug.
//   DUP-WAY     one hart must not hold the same tag valid in two ways of a set.
//               Not a coherence property — it catches the fill/replacement
//               races that put two partial versions of a line in one cache
//               (CO-7, read-miss-overtakes-writeback).
//
// Geometry is DERIVED, not copied. The older probes in this directory hardcode
// 128 sets / tagSize 19 / tagSection 23, which was correct when the D-cache was
// 32 KB. It is now 64 KB, so the tag entry is 88 bits over 256 sets and those
// constants are silently wrong. See the static_asserts below: if the cache is
// resized again this file fails to compile instead of checking the wrong bits.
//
// Transients. A raw cycle-level check on the tag arrays will also see the
// LEGITIMATE windows where a line is mid-transition: during a fill or a snoop
// downgrade there can be cycles where two caches momentarily look like owners
// before the loser's tag is updated. Those are not bugs. So this probe does not
// merely count violations, it TIMES them: each (set,tag) violation is opened
// when first seen and closed when it clears, and the run ends with a histogram
// of violation durations. That distribution is the measurement that sets the
// reporting threshold -- and it is also the argument, because a real
// dual-Unique NEVER clears (in CO-1 the losing hart's tag never changed again),
// while a transition window is a handful of cycles. Violations still open at
// the end of the run are reported separately; those are the interesting ones.
//
// Cost. The naive form re-decodes 4 cores x 256 sets x 4 ways every cycle. This
// version instead memcmp's each core's 12-byte tag row against a shadow copy
// (1024 rows/cycle) and only re-checks a set when one of its rows actually
// changed. That is sound: a line occupies the same set index in every cache, so
// a violation can only be created by a write to one of the participating rows.
//
// Build: make build/swmr_probe.out
// Run  : build/swmr_probe.out --image bins/vvadd-s1-q4.bin [--timeout N]
//                             [--persist N] [--max-report N]
//                             [--from C] [--to C] [--quiet] [--nocheck]
// Exit : 0 = clean, 1 = violation(s), 2 = usage/setup error.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>

#include "sim/rtl/rtl_model.h"

// ── Geometry, mirrored from Dcache/constants.scala + cacheLookupUnit.scala ──
// cacheSize 64 KB, lineSize 64 B, nway 4, addrWidth 32
//   dataDepth     = cacheSize*1024 / (lineSize*nway) = 256          (sets)
//   dataAddrWidth = log2(256) = 8
//   tagSize       = addrWidth - dataAddrWidth - log2(lineSize) = 32-8-6 = 18
//   tagSection    = 4 + tagSize = 22        tagDataWidth = 4*22 = 88 bits
// Tag entry layout (cacheLookupUnit.scala:142):  PLRU | Shared | Modified | Valid | tag
static const int kCacheKB    = 64;
static const int kLineSize   = 64;
static const int kNway       = 4;
static const int kAddrWidth  = 32;

static const int kNsets      = (kCacheKB * 1024) / (kLineSize * kNway);   // 256
static const int kSetShift   = 6;                                          // log2(lineSize)
static const int kSetIdxBits = 8;                                          // log2(kNsets)
static const int kTagSize    = kAddrWidth - kSetIdxBits - kSetShift;       // 18
static const int kTagSection = 4 + kTagSize;                               // 22
static const int kTagWords   = (kNway * kTagSection + 31) / 32;            // 3

static_assert(kNsets == 256,      "set count changed - re-derive from constants.scala");
static_assert(kTagSection == 22,  "tag section changed - re-derive; old probes assume 23");
static_assert(kTagWords == 3,     "tag row word count changed - check tagBRAM in system.v");

// Extract [lo +: len] from a little-endian array of 32-bit words.
static inline uint32_t wbits(const uint32_t *w, int lo, int len) {
  const int wi = lo >> 5, off = lo & 31;
  uint64_t v = (uint64_t)w[wi] >> off;
  if (off + len > 32) v |= (uint64_t)w[wi + 1] << (32 - off);
  return (uint32_t)(v & ((len >= 32) ? 0xffffffffu : ((1u << len) - 1u)));
}

struct Way { uint32_t tag; uint8_t valid, dirty, shared; };

int main(int argc, char **argv) {
  std::string image = "bins/vvadd-s1-q4.bin";
  std::string dtb = "sim/data/qemu.dtb", boot = "sim/data/boot.bin";
  uint64_t timeout = 2000000, from = 0, to = ~0ULL;
  uint64_t persist = 1;   // report a violation only once it has lasted this long
  int max_report = 20;
  bool quiet = false, nocheck = false;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> const char * {
      if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); }
      return argv[++i];
    };
    if      (a == "--image")      image = next();
    else if (a == "--dtb")        dtb = next();
    else if (a == "--boot")       boot = next();
    else if (a == "--timeout")    timeout = strtoull(next(), nullptr, 0);
    else if (a == "--from")       from = strtoull(next(), nullptr, 0);
    else if (a == "--to")         to = strtoull(next(), nullptr, 0);
    else if (a == "--persist")    persist = strtoull(next(), nullptr, 0);
    else if (a == "--max-report") max_report = atoi(next());
    else if (a == "--quiet")      quiet = true;
    // Same binary, same I/O, checking disabled -- isolates the cost of the
    // invariant itself rather than comparing against a different harness.
    else if (a == "--nocheck")     nocheck = true;
    else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
  }

  simulator bench;
  bench.init(image, dtb, boot);
  Vsystem *tb = bench.raw();

  // Verilator flattens the hierarchy, so the four tag arrays are distinct
  // members with no runtime index. Bind them once.
#define TAGMEM(N) ((const uint32_t (*)[kTagWords])tb->system__DOT__chiron__DOT__core##N##__DOT__memAccess__DOT__cacheLookup__DOT__tagBRAM__DOT__mem)
  const uint32_t (*tagmem[4])[kTagWords] = { TAGMEM(0), TAGMEM(1), TAGMEM(2), TAGMEM(3) };
#undef TAGMEM

  // Shadow copy of every core's tag rows, so a cycle costs 4x256 memcmp of 12
  // bytes rather than 4096 bitfield decodes.
  static uint32_t shadow[4][kNsets][kTagWords];
  std::memset(shadow, 0, sizeof shadow);

  uint64_t cyc = 0, checks = 0, reported = 0;
  // Census: a checker that observes nothing always reports CLEAN, so measure
  // that the cache is actually being watched. peak_valid is the largest number
  // of valid ways seen across all four caches at once; distinct counts unique
  // (core,set,tag) lines ever observed valid.
  uint64_t peak_valid = 0;
  std::map<uint64_t, char> seen_lines;

  // A violation is identified by (kind, set, tag) and TIMED, not just counted:
  // opened when first observed, closed when it clears. Key packs set(8) and
  // tag(18) with a 2-bit kind.
  enum Kind { K_SWMR = 0, K_MULTIDIRTY = 1, K_DUPWAY = 2 };
  static const char *kKindName[3] = { "SWMR", "MULTI-DIRTY", "DUP-WAY" };
  auto key = [](int kind, int set, uint32_t tag) -> uint32_t {
    return ((uint32_t)kind << 26) | ((uint32_t)set << kTagSize) | tag;
  };
  std::map<uint32_t, uint64_t> open_at;      // key -> cycle first observed
  uint64_t episodes[3] = {}, cycles_in[3] = {};
  // Duration histogram: 1, 2-3, 4-7, 8-15, 16-63, 64-255, 256-1023, 1024+
  uint64_t hist[3][8] = {};
  auto bucket = [](uint64_t d) {
    if (d <= 1) return 0; if (d <= 3) return 1; if (d <= 7) return 2;
    if (d <= 15) return 3; if (d <= 63) return 4; if (d <= 255) return 5;
    if (d <= 1023) return 6; return 7;
  };

  auto decode = [&](int core, int set, Way out[kNway]) {
    const uint32_t *row = tagmem[core][set];
    for (int w = 0; w < kNway; w++) {
      const int b = w * kTagSection;
      out[w].tag    = wbits(row, b, kTagSize);
      out[w].valid  = (uint8_t)wbits(row, b + kTagSize + 0, 1);
      out[w].dirty  = (uint8_t)wbits(row, b + kTagSize + 1, 1);
      out[w].shared = (uint8_t)wbits(row, b + kTagSize + 2, 1);
    }
  };

  auto show = [&](const char *what, int kind, int set, uint32_t tag,
                  uint64_t since, const Way st[4][kNway]) {
    if (quiet || reported >= (uint64_t)max_report) return;
    reported++;
    const uint64_t line = ((uint64_t)tag << (kSetIdxBits + kSetShift)) |
                          ((uint64_t)set << kSetShift);
    std::printf("%12" PRIu64 "  %-11s %-9s set=%3d tag=%05x line~0x%08" PRIx64
                "  held %" PRIu64 " cyc\n",
                cyc, kKindName[kind], what, set, tag, line, cyc - since);
    for (int c = 0; c < 4; c++)
      for (int w = 0; w < kNway; w++)
        if (st[c][w].valid && st[c][w].tag == tag)
          std::printf("               hart%d way%d  V%u M%u S%u\n",
                      c, w, st[c][w].valid, st[c][w].dirty, st[c][w].shared);
    std::fflush(stdout);
  };

  // Re-check one set across all four caches. Called only when a row changed.
  auto check_set = [&](int set) {
    Way st[4][kNway];
    for (int c = 0; c < 4; c++) decode(c, set, st[c]);
    checks++;
    for (int c = 0; c < 4; c++)
      for (int w = 0; w < kNway; w++)
        if (st[c][w].valid)
          seen_lines[((uint64_t)c << 40) | ((uint64_t)set << kTagSize) | st[c][w].tag] = 1;

    // Tags currently violating in this set, by kind.
    std::map<uint32_t, int> now;   // key -> kind (for reporting convenience)

    for (int c = 0; c < 4; c++) {
      for (int w = 0; w < kNway; w++) {
        if (!st[c][w].valid) continue;
        const uint32_t tag = st[c][w].tag;

        for (int w2 = w + 1; w2 < kNway; w2++)
          if (st[c][w2].valid && st[c][w2].tag == tag)
            now[key(K_DUPWAY, set, tag)] = K_DUPWAY;

        // Evaluate each tag once, from its lowest-numbered holder.
        bool lowest = true;
        for (int c2 = 0; c2 < c && lowest; c2++)
          for (int w2 = 0; w2 < kNway; w2++)
            if (st[c2][w2].valid && st[c2][w2].tag == tag) { lowest = false; break; }
        if (!lowest) continue;

        int owners = 0, dirty = 0;
        for (int c2 = 0; c2 < 4; c2++) {
          bool uniq = false, dty = false;
          for (int w2 = 0; w2 < kNway; w2++) {
            if (!st[c2][w2].valid || st[c2][w2].tag != tag) continue;
            if (!st[c2][w2].shared) uniq = true;
            if (st[c2][w2].dirty)   dty  = true;
          }
          owners += uniq; dirty += dty;
        }
        if (owners > 1) now[key(K_SWMR, set, tag)] = K_SWMR;
        if (dirty  > 1) now[key(K_MULTIDIRTY, set, tag)] = K_MULTIDIRTY;
      }
    }

    // Open newly-violating entries.
    for (auto &kv : now)
      if (open_at.find(kv.first) == open_at.end()) open_at[kv.first] = cyc;

    // Close entries in THIS set that are no longer violating.
    const uint32_t lo = ((uint32_t)set << kTagSize);
    for (auto it = open_at.begin(); it != open_at.end();) {
      const uint32_t k = it->first;
      const int kind = (int)(k >> 26);
      if (((k & ~(3u << 26)) & ~((1u << kTagSize) - 1u)) != lo) { ++it; continue; }
      if (now.count(k)) { ++it; continue; }
      const uint64_t dur = cyc - it->second;
      episodes[kind]++; cycles_in[kind] += dur; hist[kind][bucket(dur)]++;
      if (dur >= persist)
        show("cleared", kind, set, k & ((1u << kTagSize) - 1u), it->second, st);
      it = open_at.erase(it);
    }
  };

  if (!quiet)
    std::printf("swmr_probe: %s  sets=%d ways=%d tagSize=%d tagSection=%d\n\n",
                image.c_str(), kNsets, kNway, kTagSize, kTagSection);

  while (cyc < timeout) {
    // Mirror simulator::tick_nodump() exactly (rtl_model.h:389) -- the leading
    // eval() settles combinational state before the edge.
    tb->eval();
    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    cyc++;

    if (cyc < from || cyc > to || nocheck) continue;

    for (int c = 0; c < 4; c++)
      for (int s = 0; s < kNsets; s++)
        if (std::memcmp(shadow[c][s], tagmem[c][s], sizeof shadow[c][s]) != 0) {
          std::memcpy(shadow[c][s], tagmem[c][s], sizeof shadow[c][s]);
          check_set(s);
        }

    if (!quiet && cyc % 500000 == 0) {
      std::fprintf(stderr, "  [cyc %" PRIu64 "  set-checks %" PRIu64
                   "  open %zu  episodes %" PRIu64 "]\n",
                   cyc, checks, open_at.size(),
                   episodes[0] + episodes[1] + episodes[2]);
      std::fflush(stderr);
    }
  }

  // Anything still open at the end never cleared. That is the dual-Unique
  // signature and the only class of alarm that is unambiguously a bug.
  uint64_t unresolved[3] = {};
  reported = 0;   // unresolved alarms get their own report budget
  for (auto &kv : open_at) {
    const int kind = (int)(kv.first >> 26);
    const int set  = (int)((kv.first >> kTagSize) & (kNsets - 1));
    const uint32_t tag = kv.first & ((1u << kTagSize) - 1u);
    unresolved[kind]++;
    Way st[4][kNway];
    for (int c = 0; c < 4; c++) decode(c, set, st[c]);
    show("UNRESOLVED", kind, set, tag, kv.second, st);
  }

  // Census sweep of the final state, as an independent check that the tag
  // arrays are the ones the cache actually uses.
  {
    uint64_t v = 0;
    for (int c = 0; c < 4; c++)
      for (int s2 = 0; s2 < kNsets; s2++) {
        Way st[kNway]; decode(c, s2, st);
        for (int w = 0; w < kNway; w++) if (st[w].valid) v++;
      }
    peak_valid = v;
  }
  std::printf("\nswmr_probe: %" PRIu64 " cycles, %" PRIu64 " set-checks\n", cyc, checks);
  std::printf("census: %zu distinct lines ever valid; %" PRIu64
              " of %d ways valid at end\n",
              seen_lines.size(), peak_valid, 4 * kNsets * kNway);
  std::printf("%-12s %10s %10s %12s\n", "invariant", "episodes", "unresolved", "cycles-in");
  for (int k = 0; k < 3; k++)
    std::printf("%-12s %10" PRIu64 " %10" PRIu64 " %12" PRIu64 "\n",
                kKindName[k], episodes[k], unresolved[k], cycles_in[k]);

  std::printf("\nviolation-duration histogram (cycles held before clearing):\n");
  std::printf("%-12s %6s %6s %6s %6s %6s %6s %6s %6s\n",
              "", "1", "2-3", "4-7", "8-15", "16-63", "64-255", "256-1k", ">=1k");
  for (int k = 0; k < 3; k++) {
    std::printf("%-12s", kKindName[k]);
    for (int b = 0; b < 8; b++) std::printf(" %6" PRIu64, hist[k][b]);
    std::printf("\n");
  }

  const uint64_t total_unres = unresolved[0] + unresolved[1] + unresolved[2];
  const uint64_t total_ep    = episodes[0] + episodes[1] + episodes[2];
  if (total_unres) std::printf("\n  *** %" PRIu64 " UNRESOLVED VIOLATION(S) ***\n", total_unres);
  else if (total_ep) std::printf("\n  no unresolved violations (%" PRIu64
                                 " transient episode(s) -- see histogram)\n", total_ep);
  else std::printf("\n  CLEAN\n");
  return total_unres ? 1 : 0;
}
