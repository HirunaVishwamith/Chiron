// kairos/oracle.h — deciding whether a run found a bug.
//
// This is the part that multicore testing gets wrong most often, so the design
// is deliberately conservative: an oracle that cries wolf destroys a fuzzing
// campaign faster than one that misses bugs, because every false alarm costs a
// human hour of triage and the tool gets switched off.
//
// Four layers, weakest oracle first. Each answers a DIFFERENT question, and
// each is independently attributable, so a report can say which layer fired
// rather than just "failed".
//
//   1. RESULT      did the workload compute the right answer?
//                  The oracle every regression already has. Silent on hangs,
//                  and silent on corruption that does not change the checked
//                  output.
//
//   2. LIVENESS    did the machine stop making progress?
//                  Two distinct failures live here and they need separate
//                  names, because they look nothing alike in a waveform:
//
//                  HANG      a hart stops retiring entirely (wedged ROB, lost
//                            wakeup, deadlocked arbiter).
//                  LIVELOCK  every hart retires happily and the workload never
//                            finishes -- a spin loop nobody ever releases.
//
//                  Both produce no values, so layers 1, 3 and every
//                  value-based multicore oracle in the literature (TSOtool,
//                  McVerSi, MTraceCheck, DiffTest) are structurally blind to
//                  them. Four of the hardest bugs in this project's history
//                  were of this kind. This layer is cheap and catches them.
//
//                  LIVELOCK is measured RELATIVE TO THE UNPERTURBED RUN: the
//                  workload completed in B cycles with no stalls, and under
//                  perturbation it has now run more than K*B without
//                  completing. Total delay is bounded (Kairos knows exactly how
//                  many hart-cycles it held), so a bounded slowdown is expected
//                  and an unbounded one is not. It is a SUSPICION, not a proof
//                  -- a test with its own bounded-wait assumption can trip it
//                  legitimately -- so it is reported under its own name and
//                  triaged, never folded in with the structural violations.
//
//   3. STRUCTURAL  did the machine enter a state it could not legally be in?
//                  SWMR and the data-value invariant, checked on live cache
//                  state, cycle by cycle. Fires at the transaction that causes
//                  the violation rather than at the outcome that eventually
//                  reveals it -- which for the dual-Unique bug was ~10^8 cycles
//                  and a different subsystem later.
//
//   4. ARCHITECTURAL  did a hart's committed state diverge from the golden
//                  model, excluding legitimately racy commits?
//                  The strongest oracle when it applies, and the one that needs
//                  a reference model.
//
// LIVENESS AND PERTURBATION INTERACT, and getting this wrong would invalidate
// every result: a hart that Kairos is deliberately stalling is *supposed* to
// stop retiring. So the liveness detector must measure progress only against
// cycles in which a hart was NOT stalled. `note_stall` exists solely for that,
// and forgetting to call it turns the tool into a false-alarm generator.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dut.h"

namespace kairos {

enum class Verdict : uint8_t {
  Ok,            // ran to completion, right answer, no invariant fired
  WrongResult,   // layer 1
  Hang,          // layer 2a — no hart is retiring at all
  Livelock,      // layer 2b — harts retire, the workload never finishes
  Structural,    // layer 3
  Divergence,    // layer 4
  Timeout,       // budget exhausted without completing; NOT a bug on its own
};

inline const char *verdict_name(Verdict v) {
  switch (v) {
    case Verdict::Ok:          return "ok";
    case Verdict::WrongResult: return "wrong-result";
    case Verdict::Hang:        return "hang";
    case Verdict::Livelock:    return "livelock";
    case Verdict::Structural:  return "structural";
    case Verdict::Divergence:  return "divergence";
    case Verdict::Timeout:     return "timeout";
  }
  return "?";
}

struct Finding {
  Verdict     verdict = Verdict::Ok;
  uint64_t    cycle   = 0;       // when the oracle fired
  int         hart    = -1;      // attributed hart, -1 if none
  std::string detail;            // human-readable, goes straight into the report
};

class Oracle {
 public:
  // `hang_cycles` is how many *unstalled* cycles without a retirement counts as
  // a hang. Too low and legitimate long-latency misses look like hangs; too
  // high and the campaign wastes budget. Reported in the run record so a result
  // can always be re-derived with a different threshold.
  // `baseline_cycles` is how long the workload took unperturbed (0 = unknown,
  // which disables the livelock layer). `livelock_factor` is the multiple of
  // that budget at which a still-running workload is called a livelock.
  Oracle(int harts, uint64_t hang_cycles, uint64_t baseline_cycles = 0,
         uint64_t livelock_factor = 100)
      : harts_(harts), hang_cycles_(hang_cycles),
        livelock_(baseline_cycles && livelock_factor
                      ? baseline_cycles * livelock_factor : 0) {
    last_progress_.assign(harts, 0);
    last_retired_.assign(harts, 0);
    stalled_for_.assign(harts, 0);
    stall_total_.assign(harts, 0);
  }

  // Call once per cycle, BEFORE stepping, with the mask about to be applied.
  void note_stall(uint32_t mask) {
    stall_mask_ = mask;
    // Counted here, unconditionally. Counting it inside the hang loop instead
    // would miss the cycles where a stalled hart still retires an in-flight
    // instruction, and under-report exactly the policies that starve hardest.
    for (int h = 0; h < harts_; h++)
      if (mask & (1u << h)) stall_total_[h]++;
  }

  // Total cycles each hart has been held, for the report and for the
  // stall-awareness rule below.
  uint64_t stalled(int h) const { return stall_total_[h]; }

  // Call once per cycle after stepping. Returns a finding as soon as any layer
  // fires; the campaign stops that run at the first finding so the reported
  // cycle is the DETECTION cycle, not some later consequence.
  Finding check(const Dut &dut) {
    Finding f;
    const uint64_t cyc = dut.cycle();

    // ── Layer 3: structural ────────────────────────────────────────────────
    if (dut.capabilities().line_state) {
      std::vector<std::string> v;
      if (dut.check_coherence(v) && !v.empty()) {
        f.verdict = Verdict::Structural;
        f.cycle = cyc;
        f.detail = v.front();
        return f;
      }
    }

    // ── Layer 2: liveness ──────────────────────────────────────────────────
    // Only cycles in which a hart was NOT stalled count against it. Without
    // this, Kairos would report its own perturbation as a hang.
    for (int h = 0; h < harts_; h++) {
      const uint64_t r = dut.retired(h);
      if (r != last_retired_[h]) {
        last_retired_[h] = r;
        last_progress_[h] = cyc;
        stalled_for_[h] = 0;
        continue;
      }
      if (stall_mask_ & (1u << h)) {
        // Deliberately stalled: this cycle does not count as lack of progress,
        // but remember how long we have held it so the report can say so.
        stalled_for_[h]++;
        last_progress_[h] = cyc;  // freeze the clock for this hart
        continue;
      }
      if (cyc - last_progress_[h] > hang_cycles_) {
        f.verdict = Verdict::Hang;
        f.cycle = cyc;
        f.hart = h;
        f.detail = "hart " + std::to_string(h) + " retired nothing for " +
                   std::to_string(hang_cycles_) +
                   " unstalled cycles (held stalled " +
                   std::to_string(stalled_for_[h]) + " cycles total)";
        return f;
      }
    }

    // ── Layer 2b: livelock ────────────────────────────────────────────────
    // Everyone is retiring, nobody is finishing. Deliberately checked AFTER
    // the hang layer so a wedged hart is reported as a hang, which is the more
    // specific diagnosis.
    //
    // STALL-AWARENESS, and this cost seven false positives to learn. The hang
    // layer has always discounted deliberately-stalled cycles; the livelock
    // layer did not, and a policy that holds a hart for most of the run then
    // makes "the workload never finished" trivially true. `pct` did exactly
    // that -- two harts retired ZERO instructions in 662,901 cycles -- and the
    // campaign dutifully reported seven livelocks in the design. They were
    // livelocks in the TOOL.
    //
    // The rule: a livelock verdict requires that no hart has been held for more
    // than 1/kStallFrac of the elapsed run. Past that, the slowdown is
    // attributable to Kairos and the run is reported as a plain `timeout`
    // instead -- which is not a finding, so nothing is claimed. Conservative on
    // purpose: an oracle that cries wolf gets switched off.
    if (livelock_ && !dut.finished() && cyc > livelock_ && !heavily_stalled(cyc)) {
      f.verdict = Verdict::Livelock;
      f.cycle = cyc;
      f.detail = "workload still running after " + std::to_string(cyc) +
                 " cycles; unperturbed it finished well before " +
                 std::to_string(livelock_) +
                 " (all harts still retiring — needs triage, see oracle.h)";
      return f;
    }

    // ── Layer 1: result ────────────────────────────────────────────────────
    if (dut.finished()) {
      if (!dut.passed()) {
        f.verdict = Verdict::WrongResult;
        f.cycle = cyc;
        f.detail = "workload completed with the wrong result";
      }
      // Ok is returned by the campaign when finished() && passed().
    }
    return f;
  }

 private:
  // A hart held for more than 1/kStallFrac of the run makes "did not finish"
  // the tool's own doing. See the livelock layer.
  static constexpr uint64_t kStallFrac = 8;

  bool heavily_stalled(uint64_t cyc) const {
    for (int h = 0; h < harts_; h++)
      if (stall_total_[h] * kStallFrac > cyc) return true;
    return false;
  }

  int harts_;
  uint64_t hang_cycles_;
  uint64_t livelock_ = 0;   // cycle count past which "still running" is a finding
  uint32_t stall_mask_ = 0;
  std::vector<uint64_t> last_progress_, last_retired_, stalled_for_, stall_total_;
};

}  // namespace kairos
