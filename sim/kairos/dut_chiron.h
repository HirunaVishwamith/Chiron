// kairos/dut_chiron.h — the Chiron binding.
//
// This is the whole design-specific half of Kairos. Everything else (policies,
// coverage, oracle, campaign) is untouched when porting to another core; you
// write one of these and you are done. It is deliberately kept small and dull
// so that it reads as a template.
//
// Chiron supplies all four capabilities:
//   retire      — robOutN_commitFired
//   coherence   — inferred from L1 tag-array transitions (see below)
//   line_state  — the same tag arrays give SWMR directly
//   architectural — a golden model exists (not wired here; see limitations)
//
// On inferring coherence events from tag state: Chiron's ACE transactions are
// not exposed as a single observable bus, but every transaction that matters
// for an interleaving ends by changing some L1's tag entry for the line. So we
// watch the four tag arrays and emit a StateChange event per changed way. That
// is *sufficient* for the coverage model (it captures who touched which line in
// what order) and it is exactly what the SWMR oracle needs anyway. It is not a
// complete transaction trace, and the report says so rather than implying the
// coverage is finer than it is.

#pragma once

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "dut.h"
#include "sim/rtl/rtl_model.h"

namespace kairos {

// ── Geometry, derived from Dcache/constants.scala + cacheLookupUnit.scala ───
// cacheSize 64 KB, lineSize 64 B, nway 4, addrWidth 32:
//   sets = 64*1024/(64*4) = 256, tagSize = 32-8-6 = 18, tagSection = 4+18 = 22
// Tag entry layout (cacheLookupUnit.scala:142): PLRU | Shared | Modified | Valid | tag
//
// These are DERIVED and asserted, not copied. The older probes in
// sim/harness/probes/ hardcode 128 sets / tagSize 19 / tagSection 23, which was
// right when the D-cache was 32 KB and is silently wrong now. A resize must
// break the build, not the measurement.
struct ChironGeom {
  static constexpr int kCacheKB   = 64;
  static constexpr int kLineSize  = 64;
  static constexpr int kNway      = 4;
  static constexpr int kAddrWidth = 32;

  static constexpr int kHarts      = 4;
  static constexpr int kNsets      = (kCacheKB * 1024) / (kLineSize * kNway);
  static constexpr int kSetShift   = 6;
  static constexpr int kSetIdxBits = 8;
  static constexpr int kTagSize    = kAddrWidth - kSetIdxBits - kSetShift;
  static constexpr int kTagSection = 4 + kTagSize;
  static constexpr int kTagWords   = (kNway * kTagSection + 31) / 32;

  static_assert(kNsets == 256,     "set count changed - re-derive from constants.scala");
  static_assert(kTagSection == 22, "tag section changed - old probes assume 23");
  static_assert(kTagWords == 3,    "tag row words changed - check tagBRAM in system.v");
};

// Completion spec: core 0 reaching any of `pcs`, optionally gated on a0.
struct DoneSpec {
  std::vector<uint64_t> pcs;
  bool     check_a0 = false;
  uint64_t a0 = 0;
  bool active() const { return !pcs.empty(); }
  bool reached(uint64_t pc) const {
    for (uint64_t p : pcs) if (p == pc) return true;
    return false;
  }
  bool ok(uint64_t a0v) const { return !check_a0 || a0v == a0; }
};

class ChironDut : public Dut {
 public:
  ChironDut(std::string dtb, std::string boot, DoneSpec done)
      : dtb_(std::move(dtb)), boot_(std::move(boot)), done_(std::move(done)) {}

  std::string name() const override { return "chiron-rv64ima-quad"; }

  DutCapabilities capabilities() const override {
    DutCapabilities c;
    c.retire = true;
    c.coherence = true;    // inferred from tag transitions; see header
    c.line_state = true;
    c.architectural = false;  // golden model not wired into Kairos yet
    c.harts = 4;
    return c;
  }

  ~ChironDut() override { release(); }

  void reset(const std::string &image) override {
    // simulator has no destructor, so the Vsystem it new'd is ours to free.
    // A campaign builds one DUT per schedule and a Vsystem is not small;
    // leaking one per run turns a 1000-run campaign into an OOM.
    release();
    bench_.reset(new simulator());
    bench_->init(image, dtb_, boot_);
    tb_ = bench_->raw();
    cycle_ = 0;
    finished_ = passed_ = false;
    std::memset(retired_, 0, sizeof retired_);
    std::memset(last_pc_, 0, sizeof last_pc_);
    std::memset(tx_prev_, 0, sizeof tx_prev_);
    std::memset(shadow_, 0, sizeof shadow_);
    bind_tagmem();
    events_.clear();
  }

  void step() override {
    events_.clear();

    // Advance exactly the way every other harness in this tree does
    // (rtl_model.h:395). Hand-rolling the eval/clock sequence here would work
    // today and silently diverge the first time the model's stepping changes.
    bench_->tick_one();
    cycle_++;

    for (int h = 0; h < 4; h++) {
      if (commit_fired(h)) {
        retired_[h]++;
        last_pc_[h] = core_pc(h);
        events_.push_back({Event::Retire, static_cast<uint8_t>(h), 0, core_pc(h)});
      }
    }

    if (console_) drain_console();

    scan_tags();
    check_done();
  }

  uint64_t cycle() const override { return cycle_; }

  void set_stall(uint32_t mask) override {
    // The one line that perturbs the schedule. See core.scala for why this can
    // only delay a hart.
    tb_->scheduleStall = static_cast<uint8_t>(mask & 0xf);
  }
  uint32_t max_stall_mask() const override { return 0xf; }

  const std::vector<Event> &events() const override { return events_; }
  uint64_t retired(int h) const override { return retired_[h & 3]; }
  uint64_t last_pc(int h) const override { return last_pc_[h & 3]; }
  void set_console(bool on) override { console_ = on; }
  uint64_t gpr(int h, int r) const override {
    return (bench_ && r >= 0 && r < 33) ? bench_->reg(h & 3, r) : 0;
  }
  bool finished() const override { return finished_; }
  bool passed() const override { return passed_; }

  bool check_coherence(std::vector<std::string> &out) const override {
    for (const auto &v : swmr_) out.push_back(v);
    return true;
  }

  simulator *sim() { return bench_.get(); }

 private:
  void release() {
    if (tb_) { delete tb_; tb_ = nullptr; }
    bench_.reset();
  }

  static uint32_t wbits(const uint32_t *w, int lo, int len) {
    const int wi = lo >> 5, off = lo & 31;
    uint64_t v = (uint64_t)w[wi] >> off;
    if (off + len > 32) v |= (uint64_t)w[wi + 1] << (32 - off);
    return (uint32_t)(v & ((len >= 32) ? 0xffffffffu : ((1u << len) - 1u)));
  }

  void bind_tagmem() {
#define KAIROS_TAGMEM(N) ((const uint32_t (*)[ChironGeom::kTagWords])tb_->system__DOT__chiron__DOT__core##N##__DOT__memAccess__DOT__cacheLookup__DOT__tagBRAM__DOT__mem)
    tagmem_[0] = KAIROS_TAGMEM(0);
    tagmem_[1] = KAIROS_TAGMEM(1);
    tagmem_[2] = KAIROS_TAGMEM(2);
    tagmem_[3] = KAIROS_TAGMEM(3);
#undef KAIROS_TAGMEM
  }

  // Each core has its own UART port and under SMP any of them can carry the
  // console, so all four are edge-detected. `valid` is held while the write is
  // buffered, hence the per-port previous-state latch.
  void drain_console() {
    const bool v[4] = {tb_->core0OutChar_valid != 0, tb_->core1OutChar_valid != 0,
                       tb_->core2OutChar_valid != 0, tb_->core3OutChar_valid != 0};
    const char b[4] = {(char)tb_->core0OutChar_byte, (char)tb_->core1OutChar_byte,
                       (char)tb_->core2OutChar_byte, (char)tb_->core3OutChar_byte};
    for (int p = 0; p < 4; p++) {
      if (v[p] && !tx_prev_[p]) { std::fputc(b[p], stdout); std::fflush(stdout); }
      tx_prev_[p] = v[p];
    }
  }

  bool commit_fired(int h) const {
    switch (h) {
      case 1: return tb_->robOut1_commitFired;
      case 2: return tb_->robOut2_commitFired;
      case 3: return tb_->robOut3_commitFired;
      default: return tb_->robOut0_commitFired;
    }
  }
  uint64_t core_pc(int h) const {
    switch (h) {
      case 1: return tb_->robOut1_pc;
      case 2: return tb_->robOut2_pc;
      case 3: return tb_->robOut3_pc;
      default: return tb_->robOut0_pc;
    }
  }

  // Diff each core's tag rows against a shadow; a changed row becomes a
  // StateChange event and is re-checked for SWMR. Only changed sets are
  // re-decoded -- sound because a line occupies the same set index in every
  // cache, so a violation can only be created by a write to a participating row.
  void scan_tags() {
    swmr_.clear();
    for (int c = 0; c < 4; c++) {
      for (int s = 0; s < ChironGeom::kNsets; s++) {
        if (std::memcmp(shadow_[c][s], tagmem_[c][s], sizeof shadow_[c][s]) == 0)
          continue;
        std::memcpy(shadow_[c][s], tagmem_[c][s], sizeof shadow_[c][s]);

        // Emit one event per valid way in the changed set, keyed by line addr.
        const uint32_t *row = tagmem_[c][s];
        for (int w = 0; w < ChironGeom::kNway; w++) {
          const int b = w * ChironGeom::kTagSection;
          if (!wbits(row, b + ChironGeom::kTagSize, 1)) continue;
          const uint32_t tag = wbits(row, b, ChironGeom::kTagSize);
          const uint64_t line =
              ((uint64_t)tag << (ChironGeom::kSetIdxBits + ChironGeom::kSetShift)) |
              ((uint64_t)s << ChironGeom::kSetShift);
          events_.push_back({Event::StateChange, (uint8_t)c, line, 0});
        }
        check_swmr(s);
      }
    }
  }

  void check_swmr(int set) {
    struct Way { uint32_t tag; uint8_t valid, dirty, shared; };
    Way st[4][ChironGeom::kNway];
    for (int c = 0; c < 4; c++) {
      const uint32_t *row = tagmem_[c][set];
      for (int w = 0; w < ChironGeom::kNway; w++) {
        const int b = w * ChironGeom::kTagSection;
        st[c][w].tag    = wbits(row, b, ChironGeom::kTagSize);
        st[c][w].valid  = (uint8_t)wbits(row, b + ChironGeom::kTagSize + 0, 1);
        st[c][w].dirty  = (uint8_t)wbits(row, b + ChironGeom::kTagSize + 1, 1);
        st[c][w].shared = (uint8_t)wbits(row, b + ChironGeom::kTagSize + 2, 1);
      }
    }
    for (int c = 0; c < 4; c++) {
      for (int w = 0; w < ChironGeom::kNway; w++) {
        if (!st[c][w].valid) continue;
        const uint32_t tag = st[c][w].tag;
        // Evaluate each tag once, from its lowest-numbered holder.
        bool lowest = true;
        for (int c2 = 0; c2 < c && lowest; c2++)
          for (int w2 = 0; w2 < ChironGeom::kNway; w2++)
            if (st[c2][w2].valid && st[c2][w2].tag == tag) { lowest = false; break; }
        if (!lowest) continue;
        int owners = 0;
        for (int c2 = 0; c2 < 4; c2++)
          for (int w2 = 0; w2 < ChironGeom::kNway; w2++)
            if (st[c2][w2].valid && st[c2][w2].tag == tag && !st[c2][w2].shared) {
              owners++; break;
            }
        if (owners > 1) {
          char buf[160];
          snprintf(buf, sizeof buf,
                   "SWMR: line set=%d tag=%05x held Unique by %d harts",
                   set, tag, owners);
          swmr_.emplace_back(buf);
        }
      }
    }
  }

  // Reaching the completion PC ENDS the run whatever a0 says; a0 decides
  // whether it passed. Gating completion itself on a0 (an easy mistake) turns
  // every legitimate "the workload ran and reported failure" into a timeout,
  // which is the one verdict Kairos deliberately refuses to count as a bug --
  // so a real wrong-result would be silently discarded.
  void check_done() {
    if (finished_) return;
    if (!tb_->robOut0_commitFired) return;
    const uint64_t pc = tb_->robOut0_pc;
    if (done_.active()) {
      if (done_.reached(pc)) {
        finished_ = true;
        passed_   = done_.ok(tb_->registersOut0_10);
      }
      return;
    }
    // ISA-test convention: a7/x17 == 93 exits; gp/x3 == 1 means pass.
    if (tb_->registersOut0_17 == 93) {
      finished_ = true;
      passed_ = (tb_->registersOut0_3 == 1);
    }
  }

  std::string dtb_, boot_;
  DoneSpec done_;
  std::unique_ptr<simulator> bench_;
  Vsystem *tb_ = nullptr;
  uint64_t cycle_ = 0;
  bool finished_ = false, passed_ = false;
  uint64_t retired_[4] = {};
  uint64_t last_pc_[4] = {};
  bool console_ = false;
  bool tx_prev_[4] = {};
  std::vector<Event> events_;
  std::vector<std::string> swmr_;
  const uint32_t (*tagmem_[4])[ChironGeom::kTagWords] = {};
  uint32_t shadow_[4][ChironGeom::kNsets][ChironGeom::kTagWords] = {};
};

}  // namespace kairos
