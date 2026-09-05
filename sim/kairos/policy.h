// kairos/policy.h — scheduling policies.
//
// A policy decides, each cycle, which harts to stall. That is the entire
// mechanism by which Kairos moves a run from one interleaving to another.
//
// Every policy is a pure function of (seed, cycle, observed progress). No
// wall-clock time, no address-space randomness, no I/O. That is what makes a
// campaign reproducible: a finding is reported as a seed, and replaying the
// seed reproduces the exact schedule that produced it. A concurrency tool that
// cannot replay its own findings is a rumour generator.
//
// Four policies ship, and three of them exist to be argued with:
//
//   Deterministic  no perturbation. The baseline: what a conventional RTL
//                  regression does today, and the control that shows a
//                  deterministic simulator explores one interleaving.
//
//   RandomDelay    each hart stalled with independent probability p per cycle.
//                  This is the honest strawman -- it is roughly what an
//                  industrial UVM testbench does with randomised delays, and if
//                  it matches PCT then PCT is not earning its complexity.
//
//   PCT            bounded priority-change scheduling, after Burckhardt et al.,
//                  "A Randomized Scheduler with Probabilistic Guarantees of
//                  Finding Bugs" (ASPLOS 2010), adapted to hardware. See the
//                  long comment on the class for what transfers and what does
//                  not -- the adaptation is not free and the paper must say so.
//
//   Windowed       stall one hart for a contiguous window at a randomly chosen
//                  point. Cheap, and surprisingly effective at opening the wide
//                  races (a walker writeback outliving a peer's refill) that
//                  per-cycle coin flips close again immediately.
//
// Adding a policy means implementing `decide` and registering it. Nothing else
// in Kairos needs to know it exists.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace kairos {

// What the policy is allowed to see. Deliberately narrow: a policy that peeks
// at design internals would be tuned to one DUT and would not port.
struct SchedState {
  uint64_t cycle;
  int      harts;
  const uint64_t *retired;  // retired[h], for progress-aware policies
};

class Policy {
 public:
  virtual ~Policy() = default;
  virtual std::string name() const = 0;
  // Bit h set => stall hart h next cycle.
  virtual uint32_t decide(const SchedState &s) = 0;
  // One-line description of the parameters actually in effect, for the report.
  virtual std::string config() const = 0;
};

// ── Deterministic: the control ──────────────────────────────────────────────
class Deterministic : public Policy {
 public:
  std::string name() const override { return "deterministic"; }
  uint32_t decide(const SchedState &) override { return 0; }
  std::string config() const override { return "no perturbation"; }
};

// ── RandomDelay: the strawman worth beating ─────────────────────────────────
class RandomDelay : public Policy {
 public:
  RandomDelay(uint64_t seed, double p) : rng_(seed), p_(p) {}
  std::string name() const override { return "random"; }
  uint32_t decide(const SchedState &s) override {
    uint32_t m = 0;
    for (int h = 0; h < s.harts; h++)
      if (unit_(rng_) < p_) m |= (1u << h);
    return m;
  }
  std::string config() const override {
    return "p=" + std::to_string(p_) + " per hart per cycle";
  }

 private:
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> unit_{0.0, 1.0};
  double p_;
};

// ── PCT, adapted to hardware ────────────────────────────────────────────────
//
// Software PCT gives a probabilistic guarantee: for a program with n threads
// and k steps, and a bug of DEPTH d (the number of ordering constraints that
// must hold for it to manifest), a random priority assignment plus d-1 randomly
// placed priority-change points finds the bug with probability at least
// 1/(n * k^(d-1)).
//
// What transfers to hardware: assign each hart a random priority; run only the
// highest-priority ready harts and stall the rest; at d-1 randomly chosen
// cycles, demote the running hart. The structure of the guarantee is intact --
// it comes from a counting argument over priority orderings, not from anything
// specific to software threads.
//
// What does NOT transfer cleanly, and the paper must say so plainly:
//
//   * `k` in software is the number of scheduling STEPS. In hardware every
//     cycle is a scheduling point, so k is the cycle count -- far larger, which
//     weakens the bound. Whether cycles are the right granularity, or whether
//     the effective k should be the number of shared-memory events (much
//     smaller, and a much stronger bound), is an open question this work should
//     measure rather than assert.
//   * BLOCKING. This is the one that had to be solved rather than noted.
//     Software PCT runs only the highest-priority ENABLED thread, and the
//     runtime knows when a thread blocks on a lock, so priority hands off and
//     the program makes progress. A hardware hart never "blocks" observably --
//     a hart spinning on a flag looks exactly like a hart doing useful work.
//     Run only the top-priority hart with no hand-off and every barrier in
//     every test deadlocks: the leader spins forever waiting for peers Kairos
//     is holding, and the campaign reports a livelock on every single seed.
//     That is a defect in the policy, not a finding about the design.
//
//     So the leader is treated as blocked when it stops RETIRING: if the
//     current top-priority hart has not committed an instruction for
//     kBlockedCycles, priority passes to the next-highest hart that is still
//     making progress. Retirement is the closest hardware analogue of "this
//     thread can still run", it is already observable through the DUT
//     interface, and it needs no design-specific knowledge -- so the policy
//     still ports. This preserves what PCT's guarantee actually rests on (a
//     random total order over harts, perturbed at d-1 random points) while
//     keeping the machine live.
//   * Bug DEPTH for a coherence race is not obviously the same notion as for a
//     data race on a shared variable. Reporting sensitivity to d is therefore
//     part of the evaluation, not a knob to be tuned until the number looks
//     good.
//
// The claim to make is "we transfer PCT's structure and measure how the bound
// behaves in this domain" -- not "we inherit PCT's guarantee".
class PCT : public Policy {
 public:
  PCT(uint64_t seed, int harts, int depth, uint64_t horizon)
      : rng_(seed), harts_(harts), depth_(depth < 1 ? 1 : depth) {
    prio_.resize(harts_);
    for (int h = 0; h < harts_; h++) prio_[h] = harts_ - h;
    std::shuffle(prio_.begin(), prio_.end(), rng_);
    // d-1 priority-change points, uniform over the run.
    std::uniform_int_distribution<uint64_t> pick(1, horizon ? horizon : 1);
    for (int i = 0; i < depth_ - 1; i++) change_.push_back(pick(rng_));
    std::sort(change_.begin(), change_.end());
    lowest_ = 0;
    last_retired_.assign(harts_, 0);
    idle_since_.assign(harts_, 0);
  }

  std::string name() const override { return "pct"; }

  uint32_t decide(const SchedState &s) override {
    // Track per-hart progress so a spinning leader can be recognised as
    // blocked (see the class comment -- without this every barrier deadlocks).
    if (s.retired) {
      for (int h = 0; h < harts_; h++) {
        // A hart WE are stalling cannot retire, so its idle clock must not run
        // -- exactly the discount the oracle applies for the same reason. Without
        // this the blocked filter is a trap door: a stalled hart looks blocked,
        // stays excluded from the leader set, and can never retire to prove
        // otherwise, so demoting the incumbent changes nothing and one hart owns
        // the machine for the whole run. Measured before the fix: `pct:3` ran
        // 669,529 cycles with one or two harts doing all the work and zero
        // cross-hart events observed.
        if (last_mask_ & (1u << h)) { idle_since_[h] = s.cycle; continue; }
        if (s.retired[h] != last_retired_[h]) {
          last_retired_[h] = s.retired[h];
          idle_since_[h] = s.cycle;
        }
      }
    }

    // At each change point, demote the currently highest-priority hart to the
    // bottom. This is what lets a low-priority hart overtake, which is exactly
    // the ordering flip a depth-d bug needs.
    while (next_ < change_.size() && s.cycle >= change_[next_]) {
      const int top = leader(s, /*ignore_blocked=*/true);
      prio_[top] = --lowest_;
      next_++;
    }

    // BOUNDED TENURE, and this is the second half of the blocking problem.
    // Treating "has not retired" as blocked handles a wedged leader but not a
    // SPINNING one: a hart in a spin loop retires happily forever, so it never
    // yields, the peers it is waiting for stay stalled, and the run starves.
    // Measured, before this existed: `pct:3` on mt-spinwait left two harts with
    // ZERO retired instructions after 662,901 cycles, on 7 of 8 seeds.
    //
    // So leadership expires. After kMaxTenure cycles the incumbent is demoted
    // exactly as at a change point. This is a DEVIATION from software PCT and
    // the paper must say so: it adds priority changes the guarantee does not
    // account for. It is not optional -- without it the policy cannot run a
    // barrier-synchronised workload at all, which is most of them.
    if (s.cycle - tenure_start_ > kMaxTenure) {
      const int stale = leader(s, /*ignore_blocked=*/true);
      prio_[stale] = --lowest_;
      tenure_start_ = s.cycle;
      forced_++;
    }

    const int top = leader(s, /*ignore_blocked=*/false);
    if (top != last_top_) { last_top_ = top; tenure_start_ = s.cycle; }
    // Stall everything except the current leader.
    uint32_t m = 0;
    for (int h = 0; h < harts_; h++)
      if (h != top) m |= (1u << h);
    last_mask_ = m;
    return m;
  }

  std::string config() const override {
    return "depth=" + std::to_string(depth_) + " changes=" +
           std::to_string(change_.size()) + " blocked-after=" +
           std::to_string(kBlockedCycles) + " max-tenure=" +
           std::to_string(kMaxTenure) + " forced-yields=" +
           std::to_string(forced_);
  }

 private:
  // How long a hart may go without retiring before the policy treats it as
  // blocked and hands priority on. Long enough that an ordinary cache miss or
  // a divide does not cause a hand-off, short enough that a spin loop does.
  static constexpr uint64_t kBlockedCycles = 200;

  // Hard cap on how long one hart may hold the machine. Generous relative to a
  // spin-loop iteration (a few cycles) and to a cache miss (tens), so it does
  // not fight the priority order in the common case -- it only stops a spinner
  // from owning the machine forever.
  static constexpr uint64_t kMaxTenure = 4096;

  // Highest-priority hart that is still making progress. If every hart looks
  // blocked (they are all spinning on each other) the highest-priority one
  // runs anyway, so the machine never freezes entirely.
  int leader(const SchedState &s, bool ignore_blocked) const {
    int best = -1;
    for (int h = 0; h < harts_; h++) {
      if (!ignore_blocked && s.cycle - idle_since_[h] > kBlockedCycles) continue;
      if (best < 0 || prio_[h] > prio_[best]) best = h;
    }
    if (best >= 0) return best;
    best = 0;
    for (int h = 1; h < harts_; h++)
      if (prio_[h] > prio_[best]) best = h;
    return best;
  }

  std::mt19937_64 rng_;
  int harts_, depth_;
  std::vector<int> prio_;
  std::vector<uint64_t> change_;
  std::vector<uint64_t> last_retired_, idle_since_;
  size_t next_ = 0;
  int lowest_;
  uint64_t tenure_start_ = 0, forced_ = 0;
  uint32_t last_mask_ = 0;
  int last_top_ = -1;
};

// ── Windowed: one hart, one contiguous stall ────────────────────────────────
// Per-cycle coin flips reopen and reclose a race immediately, which is wrong
// for the bug shapes that motivated this work: a fence.i walker writeback has
// to stay live ACROSS a peer's refill, which needs a wide, contiguous delay.
class Windowed : public Policy {
 public:
  Windowed(uint64_t seed, int harts, uint64_t horizon, uint64_t max_width)
      : rng_(seed), harts_(harts) {
    std::uniform_int_distribution<int> ph(0, harts_ - 1);
    std::uniform_int_distribution<uint64_t> at(0, horizon ? horizon : 1);
    std::uniform_int_distribution<uint64_t> wd(1, max_width ? max_width : 1);
    hart_ = ph(rng_);
    start_ = at(rng_);
    width_ = wd(rng_);
  }
  std::string name() const override { return "windowed"; }
  uint32_t decide(const SchedState &s) override {
    if (s.cycle >= start_ && s.cycle < start_ + width_) return 1u << hart_;
    return 0;
  }
  std::string config() const override {
    return "hart=" + std::to_string(hart_) + " start=" + std::to_string(start_) +
           " width=" + std::to_string(width_);
  }

 private:
  std::mt19937_64 rng_;
  int harts_, hart_;
  uint64_t start_, width_;
};

// ── Factory ─────────────────────────────────────────────────────────────────
// `spec` is "name" or "name:param":
//
//   deterministic        no perturbation (the control)
//   random[:p]           p = per-hart per-cycle stall probability (default 0.02)
//   pct[:d]              d = assumed bug depth (default 3)
//   windowed[:w]         w = maximum window width in cycles (default 4096)
//
// Unknown names fall back to Deterministic and say so on stderr rather than
// aborting: a sweep that silently drops a policy would report a comparison
// between fewer arms than the caller asked for, which is worse than noisy.
inline std::unique_ptr<Policy> make_policy(const std::string &spec, uint64_t seed,
                                           int harts, uint64_t horizon) {
  const size_t colon = spec.find(':');
  const std::string name = spec.substr(0, colon);
  const std::string arg  = colon == std::string::npos ? "" : spec.substr(colon + 1);

  if (name == "deterministic" || name == "none" || name == "det")
    return std::unique_ptr<Policy>(new Deterministic());
  if (name == "random" || name == "rand")
    return std::unique_ptr<Policy>(
        new RandomDelay(seed, arg.empty() ? 0.02 : std::stod(arg)));
  if (name == "pct")
    return std::unique_ptr<Policy>(
        new PCT(seed, harts, arg.empty() ? 3 : std::stoi(arg), horizon));
  if (name == "windowed" || name == "window")
    return std::unique_ptr<Policy>(
        new Windowed(seed, harts, horizon,
                     arg.empty() ? 4096 : std::stoull(arg)));

  std::fprintf(stderr,
               "kairos: unknown policy '%s'; using deterministic. "
               "Known: deterministic, random[:p], pct[:depth], windowed[:width]\n",
               name.c_str());
  return std::unique_ptr<Policy>(new Deterministic());
}

}  // namespace kairos
