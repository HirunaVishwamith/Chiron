// kairos/campaign.h — one schedule, executed.
//
// Everything Kairos does is built from a single primitive: run the workload
// once under a given policy and report what happened. `run_once` is that
// primitive, and it is deliberately the ONLY place a DUT is stepped, so the
// campaign, the replayer and the shrinker cannot drift apart in how they drive
// the design. A shrinker whose predicate ran the DUT slightly differently from
// the campaign that found the bug would "reduce" schedules that no longer
// reproduce anything.
//
// The per-cycle loop is four operations and their ORDER is load bearing:
//
//   1. policy decides the stall mask for the coming cycle
//   2. the oracle is told about that mask   (so it does not count a deliberate
//      stall as a hang -- see oracle.h)
//   3. the DUT steps one cycle
//   4. coverage folds in the events, then the oracle checks
//
// Getting 1 and 2 the other way round is the single easiest way to turn this
// tool into a false-alarm generator, which is why note_stall takes the mask
// rather than reading it back from the DUT.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "coverage.h"
#include "dut_chiron.h"
#include "oracle.h"
#include "policy.h"

namespace kairos {

// Everything needed to reproduce a run except the schedule itself.
struct RunSpec {
  std::string image;
  std::string dtb  = "sim/data/qemu.dtb";
  std::string boot = "sim/data/boot.bin";
  DoneSpec    done;
  uint64_t    budget = 2000000;   // cycle cap for one run
  uint64_t    hang   = 200000;    // unstalled cycles with no retire => Hang
  uint64_t    baseline = 0;       // unperturbed length; 0 disables the livelock layer
  uint64_t    livelock_factor = 100;  // multiple of `baseline` that counts as a livelock
  bool        console = false;    // echo guest UART output to stdout
  // Sample per-hart progress every `timeline_every` cycles into `timeline`.
  // 0 = off. A liveness finding is a statement about WHEN each hart stopped
  // moving, and that is unreadable as a pair of scalars.
  uint64_t    timeline_every = 0;
};

// One sample of the machine's progress.
struct TimelinePoint {
  uint64_t cycle = 0;
  uint64_t retired[8] = {};
  uint32_t stall_mask = 0;
};

struct RunResult {
  Verdict     verdict    = Verdict::Ok;
  uint64_t    cycles     = 0;   // cycles actually executed
  uint64_t    find_cycle = 0;   // cycle the oracle fired, 0 if it did not
  uint64_t    digest     = 0;   // schedule digest for this run
  int         hart       = -1;
  bool        finished   = false;
  bool        fresh      = false;  // this interleaving had not been seen before
  std::string detail;
  std::string policy_name, policy_config;
  uint64_t    retired_total = 0;
  std::vector<TimelinePoint> timeline;
  uint64_t    per_hart_retired[8] = {};
  uint64_t    per_hart_pc[8] = {};
  uint64_t    gpr[8][33] = {};
  int         harts = 0;
};

// `cov` may be null (the shrinker does not care about coverage and rebuilding
// the digest for thousands of candidate schedules is pure cost).
inline RunResult run_once(const RunSpec &spec, Policy &pol, Coverage *cov) {
  ChironDut dut(spec.dtb, spec.boot, spec.done);
  dut.set_console(spec.console);
  dut.reset(spec.image);

  Oracle oracle(dut.capabilities().harts, spec.hang, spec.baseline,
                spec.livelock_factor);
  RunResult r;
  r.policy_name   = pol.name();
  r.policy_config = pol.config();

  const int harts = dut.capabilities().harts;
  uint64_t retired[8] = {};

  while (dut.cycle() < spec.budget) {
    for (int h = 0; h < harts; h++) retired[h] = dut.retired(h);
    const SchedState st{dut.cycle(), harts, retired};

    const uint32_t mask = pol.decide(st) & dut.max_stall_mask();
    oracle.note_stall(mask);      // BEFORE the step; see header
    dut.set_stall(mask);

    dut.step();
    if (cov) cov->observe(dut.events());

    if (spec.timeline_every && dut.cycle() % spec.timeline_every == 0) {
      TimelinePoint pt;
      pt.cycle = dut.cycle();
      pt.stall_mask = mask;
      for (int h = 0; h < harts; h++) pt.retired[h] = dut.retired(h);
      r.timeline.push_back(pt);
    }

    const Finding f = oracle.check(dut);
    if (f.verdict != Verdict::Ok) {
      r.verdict    = f.verdict;
      r.find_cycle = f.cycle;
      r.hart       = f.hart;
      r.detail     = f.detail;
      break;
    }
    if (dut.finished()) break;
  }

  r.cycles   = dut.cycle();
  r.finished = dut.finished();
  r.harts    = harts;
  for (int h = 0; h < harts; h++) {
    r.per_hart_retired[h] = dut.retired(h);
    r.per_hart_pc[h]      = dut.last_pc(h);
    r.retired_total      += dut.retired(h);
    for (int g = 0; g < 33; g++) r.gpr[h][g] = dut.gpr(h, g);
  }

  // A run that neither completed nor tripped an oracle simply ran out of
  // budget. That is NOT a finding: it is an under-provisioned campaign, and
  // conflating the two would let a tool claim bugs it never demonstrated.
  if (r.verdict == Verdict::Ok && !r.finished) r.verdict = Verdict::Timeout;

  if (cov) {
    r.digest = cov->current_digest();
    r.fresh  = cov->commit_run();
  }
  return r;
}

// A finding is a verdict that indicts the DESIGN. Timeout does not; neither
// does Ok. Kept as one function so every call site agrees.
inline bool is_finding(Verdict v) {
  return v != Verdict::Ok && v != Verdict::Timeout;
}

}  // namespace kairos
