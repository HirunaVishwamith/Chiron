// Kairos self-tests — the design-independent half, checked without any RTL.
//
// Why this exists. Every serious bug this tool has had was in its own
// reasoning, not in the simulator: a livelock layer that forgot to discount
// deliberate stalls and reported seven bugs that were not there; a "blocked"
// test that marked a stalled hart blocked and starved the machine; a coverage
// digest that would have counted run length as exploration. None of those are
// visible in a passing campaign — a broken tool produces confident output. So
// the properties the tool's conclusions rest on are asserted here, against a
// mock DUT, in a binary that links no Verilated model and runs in under a
// second.
//
// The rule these tests encode, stated once: ALWAYS SUBTRACT KAIROS'S OWN
// PERTURBATION BEFORE DRAWING A CONCLUSION. Several tests below exist purely to
// stop that being forgotten a fourth time.
//
// Build & run: make kairos-test

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "sim/kairos/coverage.h"
#include "sim/kairos/dut.h"
#include "sim/kairos/oracle.h"
#include "sim/kairos/policy.h"
#include "sim/kairos/schedule.h"
#include "sim/kairos/shrink.h"

using namespace kairos;

// ── Minimal test harness ────────────────────────────────────────────────────
static int g_fail = 0, g_run = 0;
static const char *g_case = "";

#define CASE(name) do { g_case = name; } while (0)
#define CHECK(cond, ...) do {                                                  \
    g_run++;                                                                   \
    if (!(cond)) {                                                             \
      g_fail++;                                                                \
      std::printf("  FAIL %s:%d [%s] ", __FILE__, __LINE__, g_case);           \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

// ── A DUT that exists only to be reasoned about ─────────────────────────────
// Retires one instruction per hart per unstalled cycle, and finishes when every
// hart has retired `target` of them. `frozen` harts never retire even when
// unstalled -- that is the wedge the liveness oracle is supposed to catch.
class MockDut : public Dut {
 public:
  MockDut(int harts, uint64_t target) : harts_(harts), target_(target) {
    retired_.assign(harts, 0);
  }
  std::string name() const override { return "mock"; }
  DutCapabilities capabilities() const override {
    DutCapabilities c; c.retire = true; c.harts = harts_; return c;
  }
  void reset(const std::string &) override {
    cycle_ = 0; std::fill(retired_.begin(), retired_.end(), 0);
  }
  void step() override {
    cycle_++;
    events_.clear();
    for (int h = 0; h < harts_; h++) {
      if (mask_ & (1u << h)) continue;
      if (frozen_ & (1u << h)) continue;
      retired_[h]++;
      events_.push_back({Event::StateChange, (uint8_t)h, line_[h], 0});
    }
  }
  uint64_t cycle() const override { return cycle_; }
  void set_stall(uint32_t m) override { mask_ = m; }
  uint32_t max_stall_mask() const override { return (1u << harts_) - 1; }
  const std::vector<Event> &events() const override { return events_; }
  uint64_t retired(int h) const override { return retired_[h]; }
  bool finished() const override {
    for (int h = 0; h < harts_; h++) if (retired_[h] < target_) return false;
    return true;
  }
  bool passed() const override { return finished(); }

  void freeze(uint32_t m) { frozen_ = m; }
  void set_line(int h, uint64_t addr) { line_[h] = addr; }

 private:
  int harts_;
  uint64_t target_, cycle_ = 0;
  uint32_t mask_ = 0, frozen_ = 0;
  uint64_t line_[8] = {0, 64, 128, 192, 256, 320, 384, 448};
  std::vector<uint64_t> retired_;
  std::vector<Event> events_;
};

// ── Schedules ───────────────────────────────────────────────────────────────
void test_schedule_roundtrip() {
  CASE("schedule round-trip");
  Schedule a;
  a.image = "bins/x.bin";
  a.policy_spec = "windowed:2000";
  a.seed = 7;
  a.spans = {{100, 200, 0x4}, {5000, 5001, 0x1}, {9000, 12000, 0x6}};
  const std::string path = "/tmp/kairos-test.ksched";
  CHECK(a.save(path), "save failed");

  Schedule b;
  CHECK(b.load(path), "load failed");
  CHECK(b.spans.size() == a.spans.size(), "span count %zu != %zu",
        b.spans.size(), a.spans.size());
  for (size_t i = 0; i < a.spans.size() && i < b.spans.size(); i++)
    CHECK(b.spans[i].start == a.spans[i].start &&
          b.spans[i].end == a.spans[i].end &&
          b.spans[i].mask == a.spans[i].mask, "span %zu differs", i);
  // Provenance must survive, or a schedule gets replayed against the wrong
  // binary and produces a confident, meaningless result.
  CHECK(b.image == a.image, "image lost: '%s'", b.image.c_str());
  CHECK(b.stall_cycles() == a.stall_cycles(), "stall cycles differ");
  std::remove(path.c_str());
}

void test_schedule_mask_at() {
  CASE("mask_at cursor");
  Schedule s;
  s.spans = {{10, 20, 0x1}, {30, 40, 0x2}};
  // Monotonic queries, which is how the campaign drives it.
  CHECK(s.mask_at(0) == 0, "before first span");
  CHECK(s.mask_at(10) == 0x1, "span start is inclusive");
  CHECK(s.mask_at(19) == 0x1, "inside span");
  CHECK(s.mask_at(20) == 0, "span end is exclusive");
  CHECK(s.mask_at(35) == 0x2, "second span");
  CHECK(s.mask_at(40) == 0, "after second span");
  // A rewind must reset the cursor, or replay-after-replay silently returns 0.
  s.rewind();
  CHECK(s.mask_at(15) == 0x1, "cursor did not rewind");
}

void test_recorder_replay_identity() {
  CASE("replay(record(P)) == P");
  const uint64_t kCycles = 20000;
  auto make = [] { return make_policy("random:0.05", 99, 4, kCycles); };

  // What the policy actually did.
  std::vector<uint32_t> direct;
  {
    auto p = make();
    uint64_t r[4] = {};
    for (uint64_t c = 0; c < kCycles; c++)
      direct.push_back(p->decide(SchedState{c, 4, r}));
  }
  // The same policy, recorded into a schedule.
  Schedule sched;
  {
    Recorder rec(make(), &sched);
    uint64_t r[4] = {};
    for (uint64_t c = 0; c < kCycles; c++) rec.decide(SchedState{c, 4, r});
    rec.finish();
  }
  // Replaying the schedule must reproduce the mask sequence exactly. If this
  // fails, every recorded finding is unreproducible and the shrinker is
  // minimising something that is not the failure.
  Replay rp(&sched);
  uint64_t r[4] = {};
  size_t mismatch = 0;
  for (uint64_t c = 0; c < kCycles; c++)
    if (rp.decide(SchedState{c, 4, r}) != direct[c]) mismatch++;
  CHECK(mismatch == 0, "%zu/%llu cycles differ after record+replay",
        mismatch, (unsigned long long)kCycles);
  CHECK(!sched.empty(), "recorder captured nothing");
}

// ── Coverage ────────────────────────────────────────────────────────────────
void test_coverage_ignores_run_length() {
  CASE("digest ignores run length");
  // Same ORDER of cross-hart events, different amount of work. These are the
  // same interleaving and must hash the same, or a longer run counts as
  // exploration and every policy looks good.
  Coverage a(4), b(4);
  const std::vector<Event> e0{{Event::StateChange, 0, 0x1000, 0}};
  const std::vector<Event> e1{{Event::StateChange, 1, 0x1000, 0}};
  const std::vector<Event> ret{{Event::Retire, 2, 0, 0xdead}};

  a.observe(e0); a.observe(e1);
  b.observe(e0);
  for (int i = 0; i < 500; i++) b.observe(ret);   // lots of work, no new order
  b.observe(e1);
  CHECK(a.current_digest() == b.current_digest(),
        "digest changed with run length: %016llx vs %016llx",
        (unsigned long long)a.current_digest(),
        (unsigned long long)b.current_digest());
}

void test_coverage_distinguishes_order() {
  CASE("digest distinguishes order");
  Coverage a(4), b(4);
  const std::vector<Event> e0{{Event::StateChange, 0, 0x1000, 0}};
  const std::vector<Event> e1{{Event::StateChange, 1, 0x1000, 0}};
  a.observe(e0); a.observe(e1);
  b.observe(e1); b.observe(e0);
  CHECK(a.current_digest() != b.current_digest(),
        "two different interleavings hashed the same");
}

void test_coverage_order_pairs() {
  CASE("order pairs");
  Coverage c(4);
  c.observe({{Event::StateChange, 0, 0x1000, 0}});
  c.observe({{Event::StateChange, 1, 0x1000, 0}});   // 0 before 1 on this line
  c.observe({{Event::StateChange, 1, 0x1000, 0}});   // same hart again: no pair
  CHECK(c.distinct_order_pairs() == 1, "expected 1 pair, got %llu",
        (unsigned long long)c.distinct_order_pairs());
  c.observe({{Event::StateChange, 0, 0x1000, 0}});   // 1 before 0: a new pair
  CHECK(c.distinct_order_pairs() == 2, "expected 2 pairs, got %llu",
        (unsigned long long)c.distinct_order_pairs());
}

void test_coverage_novelty() {
  CASE("novelty rate");
  Coverage c(4);
  for (int i = 0; i < 4; i++) {           // four identical runs
    c.observe({{Event::StateChange, 0, 0x1000, 0}});
    c.commit_run();
  }
  CHECK(c.distinct_schedules() == 1, "identical runs counted as distinct");
  CHECK(c.novelty_rate() == 0.25, "novelty %.3f != 0.25", c.novelty_rate());
}

// ── Oracle: the perturbation discount ───────────────────────────────────────
static Finding drive(MockDut &dut, Oracle &o, uint32_t mask, uint64_t cycles) {
  Finding last;
  for (uint64_t i = 0; i < cycles; i++) {
    o.note_stall(mask);
    dut.set_stall(mask);
    dut.step();
    last = o.check(dut);
    if (last.verdict != Verdict::Ok) return last;
  }
  return last;
}

void test_hang_discounts_stalls() {
  CASE("hang: a stalled hart is not a hang");
  MockDut dut(4, 1000000);
  dut.reset("");
  Oracle o(4, /*hang*/1000);
  // Hart 0 held for far longer than the hang threshold. It retires nothing --
  // because we are holding it. Reporting that as a hang would make the tool a
  // false-alarm generator.
  const Finding f = drive(dut, o, 0x1, 5000);
  CHECK(f.verdict == Verdict::Ok, "reported %s for a deliberately stalled hart",
        verdict_name(f.verdict));
}

void test_hang_catches_real_wedge() {
  CASE("hang: an unstalled wedged hart is a hang");
  MockDut dut(4, 1000000);
  dut.reset("");
  dut.freeze(0x2);                     // hart 1 wedges on its own
  Oracle o(4, /*hang*/1000);
  const Finding f = drive(dut, o, 0x0, 5000);
  CHECK(f.verdict == Verdict::Hang, "expected hang, got %s",
        verdict_name(f.verdict));
  CHECK(f.hart == 1, "blamed hart %d, expected 1", f.hart);
}

void test_hang_after_release_is_not_immediate() {
  CASE("hang: releasing a long stall does not fire instantly");
  // The subtle half of the discount, and the one a constant-mask test misses
  // entirely: while a hart is held the hang check is skipped anyway, so the
  // line that freezes its progress clock only matters at the RELEASE. Without
  // it, a hart held for longer than the threshold is reported as hung on its
  // very first unstalled cycle -- before it has had any chance to retire.
  MockDut dut(4, 1000000);
  dut.reset("");
  Oracle o(4, /*hang*/1000);
  drive(dut, o, 0x1, 5000);            // hold hart 0 well past the threshold
  dut.freeze(0x1);                     // released, but not yet retiring
  const Finding f = drive(dut, o, 0x0, 100);   // only 100 unstalled cycles
  CHECK(f.verdict == Verdict::Ok,
        "reported %s %d cycles after releasing a 5000-cycle stall",
        verdict_name(f.verdict), 100);
}

void test_livelock_discounts_stalls() {
  CASE("livelock: heavy stalling is not a finding");
  // THE regression for the seven false positives. A policy that holds a hart
  // for most of the run makes "did not finish" trivially true.
  MockDut dut(4, 1000000000);          // never finishes
  dut.reset("");
  Oracle o(4, /*hang*/1000000, /*baseline*/100, /*factor*/10);
  const Finding f = drive(dut, o, 0xE, 5000);   // 3 of 4 harts held throughout
  CHECK(f.verdict != Verdict::Livelock,
        "reported a livelock while holding 3 of 4 harts for the whole run");
}

void test_livelock_fires_when_perturbation_is_light() {
  CASE("livelock: light stalling still reports");
  MockDut dut(4, 1000000000);          // never finishes
  dut.reset("");
  Oracle o(4, /*hang*/1000000, /*baseline*/100, /*factor*/10);
  // No stalls at all: nothing to discount, so a workload that never completes
  // past 10x its baseline is a genuine livelock.
  const Finding f = drive(dut, o, 0x0, 5000);
  CHECK(f.verdict == Verdict::Livelock, "expected livelock, got %s",
        verdict_name(f.verdict));
}

void test_livelock_disabled_without_baseline() {
  CASE("livelock: disabled when the baseline is unknown");
  MockDut dut(4, 1000000000);
  dut.reset("");
  Oracle o(4, 1000000, /*baseline*/0, /*factor*/10);
  const Finding f = drive(dut, o, 0x0, 5000);
  CHECK(f.verdict == Verdict::Ok, "fired without a baseline: %s",
        verdict_name(f.verdict));
}

// ── Policies ────────────────────────────────────────────────────────────────
void test_deterministic_never_stalls() {
  CASE("deterministic never stalls");
  auto p = make_policy("deterministic", 1, 4, 10000);
  uint64_t r[4] = {};
  uint32_t acc = 0;
  for (uint64_t c = 0; c < 10000; c++) acc |= p->decide(SchedState{c, 4, r});
  CHECK(acc == 0, "the control policy perturbed the run (mask 0x%x)", acc);
}

void test_unknown_policy_falls_back() {
  CASE("unknown policy falls back, does not abort");
  auto p = make_policy("nonsense:42", 1, 4, 1000);
  CHECK(p != nullptr, "factory returned null");
  CHECK(p->name() == "deterministic", "fell back to '%s'", p->name().c_str());
}

void test_pct_always_leaves_someone_running() {
  CASE("pct always leaves a hart running");
  auto p = make_policy("pct:3", 5, 4, 100000);
  uint64_t r[4] = {};
  for (uint64_t c = 0; c < 100000; c++) {
    const uint32_t m = p->decide(SchedState{c, 4, r});
    CHECK(m != 0xF, "stalled every hart at cycle %llu", (unsigned long long)c);
    if (m == 0xF) break;
    for (int h = 0; h < 4; h++) if (!(m & (1u << h))) r[h]++;
  }
}

void test_pct_bounded_tenure() {
  CASE("pct: no hart is starved forever");
  // The regression for the starvation bug: `pct` once left two harts with zero
  // retired instructions across the entire run, because a spinning leader never
  // looked blocked and a stalled hart could never prove it was not.
  auto p = make_policy("pct:3", 5, 4, 200000);
  uint64_t r[4] = {};
  for (uint64_t c = 0; c < 200000; c++) {
    const uint32_t m = p->decide(SchedState{c, 4, r});
    for (int h = 0; h < 4; h++) if (!(m & (1u << h))) r[h]++;   // spinning: always retires
  }
  for (int h = 0; h < 4; h++)
    CHECK(r[h] > 0, "hart %d ran for 0 cycles out of 200000", h);
}

void test_windowed_is_one_contiguous_span() {
  CASE("windowed: one hart, one contiguous window");
  auto p = make_policy("windowed:500", 3, 4, 20000);
  uint64_t r[4] = {};
  int transitions = 0;
  uint32_t prev = 0, seen = 0;
  for (uint64_t c = 0; c < 20000; c++) {
    const uint32_t m = p->decide(SchedState{c, 4, r});
    seen |= m;
    if (m != prev) { transitions++; prev = m; }
  }
  CHECK(transitions <= 2, "%d mask transitions; expected one contiguous window",
        transitions);
  CHECK((seen & (seen - 1)) == 0, "held more than one hart (mask 0x%x)", seen);
}

// ── Shrinker ────────────────────────────────────────────────────────────────
void test_shrink_finds_minimal_span_set() {
  CASE("shrink reduces to the essential span");
  Schedule s;
  for (uint64_t i = 0; i < 64; i++)
    s.spans.push_back({i * 1000, i * 1000 + 100, 0x1});
  s.spans.push_back({999000, 999500, 0x4});      // the one that matters

  int trials = 0;
  Shrinker sh([&](Schedule &c) {
    trials++;
    for (const Span &sp : c.spans) if (sp.mask == 0x4) return true;
    return false;
  }, /*verbose*/false);

  const ShrinkStats st = sh.run(s);
  CHECK(st.spans_after == 1, "reduced to %zu spans, expected 1", st.spans_after);
  CHECK(!s.spans.empty() && s.spans[0].mask == 0x4, "kept the wrong span");
  CHECK(st.spans_before == 65, "recorded %zu spans before", st.spans_before);
  CHECK(trials > 0, "predicate never called");
}

void test_shrink_narrows_width() {
  CASE("shrink narrows span width");
  Schedule s;
  s.spans = {{1000, 100000, 0x2}};      // 99,000 cycles wide
  // Only cycles [50000, 50010) actually matter.
  Shrinker sh([](Schedule &c) {
    return !c.spans.empty() && c.spans[0].start <= 50000 && c.spans[0].end >= 50010;
  }, false);
  const ShrinkStats st = sh.run(s);
  CHECK(s.spans.size() == 1, "span count changed");
  CHECK(st.cycles_after < st.cycles_before / 100,
        "width %llu -> %llu is not a real reduction",
        (unsigned long long)st.cycles_before, (unsigned long long)st.cycles_after);
  CHECK(s.spans[0].start <= 50000 && s.spans[0].end >= 50010,
        "narrowed past the failing region: [%llu,%llu)",
        (unsigned long long)s.spans[0].start, (unsigned long long)s.spans[0].end);
}

void test_shrink_is_deterministic() {
  CASE("shrink is deterministic");
  auto build = [] {
    Schedule s;
    for (uint64_t i = 0; i < 40; i++)
      s.spans.push_back({i * 500, i * 500 + 50, (uint32_t)(1u << (i % 4))});
    return s;
  };
  auto pred = [](Schedule &c) {
    uint64_t held = 0;
    for (const Span &sp : c.spans) held += sp.width();
    return held >= 300;               // arbitrary but order-sensitive
  };
  Schedule a = build(), b = build();
  Shrinker s1(pred, false), s2(pred, false);
  const ShrinkStats r1 = s1.run(a), r2 = s2.run(b);
  CHECK(r1.trials == r2.trials, "trial counts differ: %d vs %d", r1.trials, r2.trials);
  CHECK(a.spans.size() == b.spans.size(), "results differ in size");
  bool same = a.spans.size() == b.spans.size();
  for (size_t i = 0; same && i < a.spans.size(); i++)
    same = a.spans[i].start == b.spans[i].start &&
           a.spans[i].end == b.spans[i].end && a.spans[i].mask == b.spans[i].mask;
  CHECK(same, "two identical reductions produced different schedules");
}

void test_shrink_respects_trial_budget() {
  CASE("shrink respects its trial budget");
  Schedule s;
  for (uint64_t i = 0; i < 200; i++) s.spans.push_back({i * 10, i * 10 + 5, 0x1});
  int calls = 0;
  Shrinker sh([&](Schedule &) { calls++; return true; }, false, /*max_trials*/7);
  const ShrinkStats st = sh.run(s);
  CHECK(calls <= 7, "ran %d trials against a budget of 7", calls);
  CHECK(st.budget_exhausted, "did not report the exhausted budget");
  // Exhaustion must be SAFE: an unanswered trial counts as "does not
  // reproduce", so the result is less reduced, never wrong.
  CHECK(!s.spans.empty(), "budget exhaustion emptied the schedule");
}

// ── main ────────────────────────────────────────────────────────────────────
int main() {
  std::printf("kairos self-tests\n");
  test_schedule_roundtrip();
  test_schedule_mask_at();
  test_recorder_replay_identity();

  test_coverage_ignores_run_length();
  test_coverage_distinguishes_order();
  test_coverage_order_pairs();
  test_coverage_novelty();

  test_hang_discounts_stalls();
  test_hang_catches_real_wedge();
  test_hang_after_release_is_not_immediate();
  test_livelock_discounts_stalls();
  test_livelock_fires_when_perturbation_is_light();
  test_livelock_disabled_without_baseline();

  test_deterministic_never_stalls();
  test_unknown_policy_falls_back();
  test_pct_always_leaves_someone_running();
  test_pct_bounded_tenure();
  test_windowed_is_one_contiguous_span();

  test_shrink_finds_minimal_span_set();
  test_shrink_narrows_width();
  test_shrink_is_deterministic();
  test_shrink_respects_trial_budget();

  std::printf("%s: %d checks, %d failure(s)\n",
              g_fail ? "FAILED" : "PASSED", g_run, g_fail);
  return g_fail ? 1 : 0;
}
