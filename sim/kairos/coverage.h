// kairos/coverage.h — how much of the schedule space has actually been seen.
//
// The claim Kairos exists to support is "a deterministic simulator explores one
// interleaving per test". That claim is empty without a way to count
// interleavings, and the counting has to be honest in a specific way: it must
// be blind to how much WORK a run did and sensitive only to the ORDER in which
// harts interacted. A metric that grows because a run was longer would make
// every policy look good.
//
// Two metrics, deliberately different in what they are sensitive to:
//
//   ScheduleDigest   a 64-bit hash of the ordered sequence of cross-hart
//                    memory events. Two runs with the same digest interleaved
//                    identically. Counting distinct digests over a campaign is
//                    the headline number: "N schedules explored".
//
//   OrderPairs       the set of observed (hart_a before hart_b on line L)
//                    orderings. Much coarser than a digest and much harder to
//                    saturate by accident, so it does not flatter a policy that
//                    merely jitters timing without changing who wins a race.
//                    This is the metric to report when a reviewer asks whether
//                    "distinct schedules" is doing real work.
//
// A digest deliberately ignores CYCLE NUMBERS. Two runs that produce the same
// order of events at different absolute times are the same interleaving, and
// counting them separately would inflate every result -- which is exactly the
// mistake a naive "hash the whole trace" metric makes.

#pragma once

#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dut.h"

namespace kairos {

class Coverage {
 public:
  // `harts` is accepted for symmetry with the rest of the interface; the
  // metrics below are hart-count agnostic by construction.
  explicit Coverage(int harts) : harts_(harts) { (void)harts_; }

  // Fold one cycle's events into the running digest for the current run.
  void observe(const std::vector<Event> &evs) {
    for (const Event &e : evs) {
      // Only cross-hart-visible events define an interleaving. Retirements are
      // per-hart progress, not order between harts, so they are excluded --
      // including them would make the digest track instruction counts and thus
      // run length, which is precisely what must not happen.
      if (e.kind == Event::Retire) continue;

      // FNV-1a over (hart, line, kind). No cycle number: see header.
      mix(static_cast<uint64_t>(e.kind));
      mix(static_cast<uint64_t>(e.hart));
      mix(e.addr >> 6);

      // Order pairs: who touched this line before whom.
      const uint64_t line = e.addr >> 6;
      auto it = last_toucher_.find(line);
      if (it != last_toucher_.end() && it->second != e.hart)
        pairs_.insert(pack(it->second, e.hart, line));
      last_toucher_[line] = e.hart;
      events_++;
    }
  }

  // Close the current run and fold its digest into the campaign totals.
  // Returns true if this interleaving had not been seen before.
  bool commit_run() {
    const bool fresh = digests_.insert(digest_).second;
    if (fresh) fresh_runs_++;
    runs_++;
    digest_ = kFnvOffset;
    last_toucher_.clear();
    return fresh;
  }

  uint64_t distinct_schedules() const { return digests_.size(); }
  uint64_t distinct_order_pairs() const { return pairs_.size(); }
  uint64_t runs() const { return runs_; }
  uint64_t fresh_runs() const { return fresh_runs_; }
  uint64_t events() const { return events_; }
  uint64_t current_digest() const { return digest_; }

  // Fraction of runs that produced an interleaving never seen before. For a
  // deterministic simulator this collapses to 1/runs -- which is the entire
  // motivation for the tool, expressed as one number.
  double novelty_rate() const {
    return runs_ ? static_cast<double>(fresh_runs_) / runs_ : 0.0;
  }

 private:
  static constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
  static constexpr uint64_t kFnvPrime  = 1099511628211ULL;

  void mix(uint64_t v) {
    for (int i = 0; i < 8; i++) {
      digest_ ^= (v >> (i * 8)) & 0xff;
      digest_ *= kFnvPrime;
    }
  }
  static uint64_t pack(uint8_t a, uint8_t b, uint64_t line) {
    return (static_cast<uint64_t>(a) << 58) | (static_cast<uint64_t>(b) << 52) |
           (line & ((1ULL << 52) - 1));
  }

  int harts_;
  uint64_t digest_ = kFnvOffset;
  uint64_t runs_ = 0, fresh_runs_ = 0, events_ = 0;
  std::unordered_set<uint64_t> digests_;
  std::set<uint64_t> pairs_;
  std::unordered_map<uint64_t, uint8_t> last_toucher_;
};

}  // namespace kairos
