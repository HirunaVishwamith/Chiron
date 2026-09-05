// kairos/shrink.h — turning a failing schedule into a bug report.
//
// A campaign that finds a bug hands you a schedule with (typically) thousands
// of stall spans spread over millions of cycles. Almost none of it is load
// bearing. Without reduction the artefact is "seed 41 fails", which tells a
// designer nothing about WHERE to look, and reviewers are right to be
// unimpressed by a fuzzer that can only say that.
//
// So Kairos shrinks. The reduction is delta debugging (Zeller & Hildebrandt,
// "Simplifying and Isolating Failure-Inducing Input", TSE 2002) applied to the
// span list, plus a second pass that narrows each surviving span's WIDTH. The
// output is typically one to three spans, and reads like:
//
//     hart 2 held for 41 cycles at 812,004  ->  SWMR violation at 812,061
//
// That is a sentence an architect can act on, and it is the difference between
// a fuzzing paper and a debugging tool.
//
// The predicate is "does this schedule still reproduce the SAME verdict?" --
// not merely "does it still fail". Reducing a hang into a different, easier
// wrong-result failure would silently retarget the shrink onto another bug.
//
// DETERMINISM IS WHAT MAKES THIS WORK AT ALL. Delta debugging on a flaky
// predicate degenerates. Here the DUT is a cycle-accurate simulator and the
// schedule is fully specified, so the predicate is a pure function: every
// candidate is tested exactly once and the answer never changes. This is a
// property software concurrency shrinkers do not get to have, and it is worth
// stating in the paper: the same determinism that makes RTL simulation blind to
// interleavings is what makes reduction over them exact.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "schedule.h"

namespace kairos {

// Returns true if the candidate schedule still reproduces the target failure.
using Reproduces = std::function<bool(Schedule &)>;

// One delta-debugging trial. Recorded so the reduction can be PLOTTED rather
// than summarised by two numbers -- a reader should be able to see ddmin
// bisecting and then the width search converging, which is the difference
// between claiming the shrinker works and showing it.
struct ShrinkTrial {
  int      index = 0;        // 1-based trial number
  int      phase = 0;        // 0 = ddmin over spans, 1 = width narrowing
  size_t   spans = 0;        // span count of the candidate
  uint64_t cycles = 0;       // hart-cycles held by the candidate
  bool     reproduced = false;
  bool     accepted = false; // did this become the new best?
};

struct ShrinkStats {
  std::vector<ShrinkTrial> trace;
  size_t   spans_before = 0, spans_after = 0;
  uint64_t cycles_before = 0, cycles_after = 0;
  int      trials = 0;
  bool     budget_exhausted = false;
};

class Shrinker {
 public:
  // `max_trials` caps the work: every trial is a full simulation run, and a
  // per-cycle random policy can record hundreds of thousands of spans. Running
  // out of budget is SAFE by construction -- an exhausted trial answers "does
  // not reproduce", which keeps the span. The result is then a valid but
  // less-reduced schedule, never a wrong one. 0 means unlimited.
  Shrinker(Reproduces repro, bool verbose, int max_trials = 0)
      : repro_(std::move(repro)), verbose_(verbose), max_trials_(max_trials) {}

  bool exhausted() const { return max_trials_ && trials_ >= max_trials_; }

  // Reduce `s` in place. Precondition: repro_(s) is already known to be true;
  // the caller has just observed the failure that produced it.
  ShrinkStats run(Schedule &s) {
    ShrinkStats st;
    st.spans_before  = s.size();
    st.cycles_before = s.stall_cycles();

    trace_ = &st.trace;
    phase_ = 0;
    remove_spans(s, st);
    phase_ = 1;
    narrow_spans(s, st);

    st.spans_after  = s.size();
    st.cycles_after = s.stall_cycles();
    st.trials = trials_;
    st.budget_exhausted = exhausted();
    return st;
  }

 private:
  bool test(Schedule &cand) {
    if (exhausted()) return false;   // see the note on max_trials
    trials_++;
    cand.rewind();
    const bool ok = repro_(cand);
    if (trace_)
      trace_->push_back({trials_, phase_, cand.size(), cand.stall_cycles(), ok, false});
    return ok;
  }
  void mark_accepted() { if (trace_ && !trace_->empty()) trace_->back().accepted = true; }

  // ── Pass 1: ddmin over the span list ──────────────────────────────────────
  // Standard delta debugging: try removing 1/n of the spans at a time; on
  // success keep the reduction and restart coarse, on failure refine the
  // granularity. Converges on a 1-minimal subset -- no single remaining span
  // can be dropped without losing the failure.
  void remove_spans(Schedule &s, ShrinkStats &st) {
    size_t n = 2;
    while (s.size() >= 2) {
      const size_t chunk = (s.size() + n - 1) / n;
      bool reduced = false;

      for (size_t i = 0; i < s.size(); i += chunk) {
        Schedule cand = s;
        const size_t lo = i, hi = std::min(i + chunk, s.spans.size());
        cand.spans.erase(cand.spans.begin() + lo, cand.spans.begin() + hi);
        if (cand.spans.empty()) continue;
        if (test(cand)) {
          mark_accepted();
          s = cand;
          n = std::max<size_t>(n - 1, 2);
          reduced = true;
          if (verbose_)
            std::printf("    shrink: %zu spans remain (%llu hart-cycles)\n",
                        s.size(), (unsigned long long)s.stall_cycles());
          break;
        }
      }
      if (!reduced) {
        if (n >= s.size()) break;
        n = std::min(n * 2, s.size());
      }
    }
    st.trials = trials_;
  }

  // ── Pass 2: narrow what survives ──────────────────────────────────────────
  // A 1-minimal span list can still contain a span 100,000 cycles wide when 40
  // cycles would do. Binary-search each span's width from both ends. This is
  // what converts "hart 2 was held somewhere in this region" into a cycle
  // number a designer can put a breakpoint on.
  void narrow_spans(Schedule &s, ShrinkStats &st) {
    for (size_t i = 0; i < s.spans.size(); i++) {
      // Trim from the front: move `start` as late as it will go.
      uint64_t lo = s.spans[i].start, hi = s.spans[i].end;
      while (hi - lo > 1) {
        const uint64_t mid = lo + (hi - lo) / 2;
        Schedule cand = s;
        cand.spans[i].start = mid;
        if (cand.spans[i].start >= cand.spans[i].end) break;
        if (test(cand)) { mark_accepted(); lo = mid; } else hi = mid;
      }
      s.spans[i].start = lo;

      // Trim from the back: pull `end` as early as it will go.
      lo = s.spans[i].start + 1; hi = s.spans[i].end;
      while (hi - lo > 1) {
        const uint64_t mid = lo + (hi - lo) / 2;
        Schedule cand = s;
        cand.spans[i].end = mid;
        if (test(cand)) { mark_accepted(); hi = mid; } else lo = mid;
      }
      s.spans[i].end = hi;

      if (verbose_)
        std::printf("    narrow: span %zu -> [%llu, %llu) mask 0x%x (%llu cycles)\n",
                    i, (unsigned long long)s.spans[i].start,
                    (unsigned long long)s.spans[i].end, s.spans[i].mask,
                    (unsigned long long)s.spans[i].width());
    }
    st.trials = trials_;
  }

  Reproduces repro_;
  bool verbose_;
  int  max_trials_ = 0;
  int  trials_ = 0;
  int  phase_ = 0;
  std::vector<ShrinkTrial> *trace_ = nullptr;
};

}  // namespace kairos
