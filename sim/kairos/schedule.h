// kairos/schedule.h — a schedule as a first-class, serialisable object.
//
// A policy is a *generator* of schedules; this is the schedule itself, in the
// only form that matters downstream: the sequence of cycles during which each
// hart was held. Making it concrete buys three things that a seed alone cannot:
//
//   REPLAY      a finding can be reproduced on a different machine, a different
//               build, or after the RTL changes, without re-running the policy
//               that produced it. A seed only reproduces a finding while the
//               policy code is byte-identical; a recorded schedule survives
//               refactoring the policy.
//
//   SHRINKING   a failing schedule can be minimised (see shrink.h). A random
//               campaign that finds a bug typically holds harts thousands of
//               times over millions of cycles, and essentially all of it is
//               irrelevant. What a human needs is "hart 2 was held for 41
//               cycles starting at 812,004" — one line, which is a bug report.
//
//   PORTABILITY a schedule is a text file. It can be checked into a repository
//               as a regression, attached to a bug report, diffed, and handed
//               to somebody who does not have the fuzzer.
//
// File format (deliberately plain text, one span per line):
//
//     # kairos schedule v1
//     # image bins/mt-llist-q4.bin
//     # policy windowed:2000 seed 41
//     812004 812045 0x4
//
// `start end mask`, half-open, mask bit h = hart h held. Cycles not covered by
// any span run unperturbed. Comments and blank lines are ignored, so a schedule
// can be hand-edited to test a hypothesis -- which is exactly what you want at
// 2am with a bug you half understand.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "policy.h"

namespace kairos {

// A half-open cycle interval [start, end) during which `mask` is applied.
struct Span {
  uint64_t start = 0;
  uint64_t end   = 0;
  uint32_t mask  = 0;
  uint64_t width() const { return end - start; }
};

class Schedule {
 public:
  std::vector<Span> spans;
  std::string image, policy_spec;
  uint64_t seed = 0;

  bool empty() const { return spans.empty(); }
  size_t size() const { return spans.size(); }

  // Total hart-cycles held: the natural "size" of a schedule, and what the
  // shrinker reports as its reduction factor.
  uint64_t stall_cycles() const {
    uint64_t t = 0;
    for (const Span &s : spans) {
      uint32_t m = s.mask;
      int bits = 0;
      while (m) { bits += m & 1; m >>= 1; }
      t += s.width() * bits;
    }
    return t;
  }

  // Queried once per cycle with a monotonically increasing cycle, so a cursor
  // makes this O(1) amortised rather than O(log n) per cycle. Kairos calls it
  // on every cycle of a multi-million-cycle run; the constant matters.
  uint32_t mask_at(uint64_t cycle) {
    if (cycle < last_query_) cursor_ = 0;   // a rewind (replay restart)
    last_query_ = cycle;
    while (cursor_ < spans.size() && spans[cursor_].end <= cycle) cursor_++;
    if (cursor_ < spans.size() && spans[cursor_].start <= cycle)
      return spans[cursor_].mask;
    return 0;
  }
  void rewind() { cursor_ = 0; last_query_ = 0; }

  bool save(const std::string &path) const {
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# kairos schedule v1\n");
    if (!image.empty())       std::fprintf(f, "# image %s\n", image.c_str());
    if (!policy_spec.empty()) std::fprintf(f, "# policy %s seed %llu\n",
                                          policy_spec.c_str(),
                                          (unsigned long long)seed);
    std::fprintf(f, "# %zu span(s), %llu hart-cycles held\n",
                 spans.size(), (unsigned long long)stall_cycles());
    for (const Span &s : spans)
      std::fprintf(f, "%llu %llu 0x%x\n", (unsigned long long)s.start,
                   (unsigned long long)s.end, s.mask);
    std::fclose(f);
    return true;
  }

  bool load(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    spans.clear();
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
      // Recover the provenance comments so a replayed schedule still knows
      // which image it belongs to; a schedule replayed against the wrong
      // binary produces a confident, meaningless result.
      if (line[0] == '#') {
        char buf[256];
        if (std::sscanf(line, "# image %255s", buf) == 1) image = buf;
        continue;
      }
      Span s;
      unsigned long long a, b;
      unsigned m;
      if (std::sscanf(line, "%llu %llu %x", &a, &b, &m) == 3) {
        s.start = a; s.end = b; s.mask = m;
        if (s.end > s.start && s.mask) spans.push_back(s);
      }
    }
    std::fclose(f);
    rewind();
    return true;
  }

 private:
  size_t   cursor_ = 0;
  uint64_t last_query_ = 0;
};

// ── Recorder: wrap any policy and capture what it actually did ──────────────
// Consecutive cycles with the same non-zero mask coalesce into one span, so a
// windowed policy records one line and even a per-cycle random policy records
// far fewer spans than cycles. Zero-mask stretches are not recorded at all --
// "unperturbed" is the default, not a fact worth storing.
class Recorder : public Policy {
 public:
  Recorder(std::unique_ptr<Policy> inner, Schedule *out)
      : inner_(std::move(inner)), out_(out) {}

  std::string name() const override { return inner_->name(); }
  std::string config() const override { return inner_->config(); }

  uint32_t decide(const SchedState &s) override {
    const uint32_t m = inner_->decide(s);
    if (m != open_mask_) {
      flush(s.cycle);
      open_mask_ = m;
      open_start_ = s.cycle;
    }
    last_cycle_ = s.cycle;
    return m;
  }

  // Must be called when the run ends, or the final span is lost.
  void finish() { flush(last_cycle_ + 1); }

 private:
  void flush(uint64_t at) {
    if (open_mask_ && at > open_start_)
      out_->spans.push_back({open_start_, at, open_mask_});
    open_mask_ = 0;
  }
  std::unique_ptr<Policy> inner_;
  Schedule *out_;
  uint32_t open_mask_ = 0;
  uint64_t open_start_ = 0, last_cycle_ = 0;
};

// ── Replay: drive a run from a recorded schedule ────────────────────────────
class Replay : public Policy {
 public:
  explicit Replay(Schedule *s) : sched_(s) { sched_->rewind(); }
  std::string name() const override { return "replay"; }
  std::string config() const override {
    return std::to_string(sched_->size()) + " span(s), " +
           std::to_string(sched_->stall_cycles()) + " hart-cycles held";
  }
  uint32_t decide(const SchedState &s) override { return sched_->mask_at(s.cycle); }

 private:
  Schedule *sched_;
};

}  // namespace kairos
