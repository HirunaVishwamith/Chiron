// kairos — schedule exploration for multicore RTL simulation.
//
// Cycle-accurate RTL simulation of a multicore is deterministic: it explores
// exactly one interleaving per test program, however many times you run it.
// Silicon does not have that problem -- clock jitter, interrupts, DRAM refresh
// and voltage droop vary the interleaving on every execution, which is why
// post-silicon multicore validation finds coherence bugs that pre-silicon
// simulation missed. Simulation loses that coverage source precisely at the
// stage where bugs are cheapest to fix.
//
// Kairos supplies the missing variation directly: it DELAYS harts under a
// controlled, reproducible policy, so one program samples many interleavings.
// Because a stall is indistinguishable from backpressure the design already
// generates for itself, every schedule it induces is one the unmodified design
// could reach -- so a failure under Kairos is a real failure, and there are no
// false positives by construction.
//
//   kairos run     --image X.bin [--policy P] [--runs N] [--shrink]
//   kairos sweep   --image X.bin --policies a,b,c --runs N
//   kairos replay  --image X.bin (--policy P --seed S | --schedule F.ksched)
//   kairos shrink  --image X.bin (--policy P --seed S | --schedule F.ksched)
//
// Every finding is (policy, seed) and replays exactly; `shrink` reduces it to
// the few stall spans that actually cause it. See sim/kairos/README.md.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "campaign.h"
#include "coverage.h"
#include "schedule.h"
#include "shrink.h"

using namespace kairos;

namespace {

struct Options {
  std::string mode = "run";
  RunSpec     spec;
  std::string policy   = "windowed";
  std::string policies;              // sweep mode
  std::string json;                  // machine-readable report
  std::string schedule_in, schedule_out;
  uint64_t runs = 32;
  uint64_t seed = 1;
  uint64_t horizon = 0;              // 0 = calibrate from an unperturbed run
  bool shrink = false;
  int  shrink_trials = 400;
  bool quiet = false;
  bool stop_on_find = false;
  bool dump_regs = false;
  std::string timeline;      // CSV of per-hart progress
};

void usage() {
  std::printf(
"kairos — schedule exploration for multicore RTL simulation\n"
"\n"
"  kairos run     --image <bin> [options]      explore N schedules\n"
"  kairos sweep   --image <bin> --policies a,b,c    compare policies\n"
"  kairos replay  --image <bin> ...            re-run one exact schedule\n"
"  kairos shrink  --image <bin> ...            reduce a failing schedule\n"
"\n"
"Workload\n"
"  --image F           the .bin to run (required)\n"
"  --dtb F --boot F    device tree / boot ROM (defaults under sim/data/)\n"
"  --done-pc H         completion PC, repeatable; without one, the ISA-test\n"
"                      convention (a7==93, gp==1 means pass) is used\n"
"  --done-a0 V         additionally require a0 == V at the completion PC\n"
"\n"
"Exploration\n"
"  --policy P          deterministic | random[:p] | pct[:depth] | windowed[:width]\n"
"  --policies a,b,c    sweep mode: the arms to compare under one budget\n"
"  --runs N            schedules per policy (default 32)\n"
"  --cycles C          cycle budget per run (default 2000000)\n"
"  --seed S            base seed; run i uses S+i (default 1)\n"
"  --horizon N         cycle range the policies spread their delays over;\n"
"                      default: measured from an unperturbed calibration run\n"
"\n"
"Oracles\n"
"  --hang N            unstalled cycles with no retirement => Hang (default 200000)\n"
"  --livelock-factor K workload still running after K x the unperturbed length\n"
"                      => Livelock (default 100; 0 disables the layer)\n"
"\n"
"Findings\n"
"  --shrink            on a finding, reduce the schedule to its essential spans\n"
"  --shrink-trials N   cap the reduction at N simulation runs (default 400)\n"
"  --schedule F        replay/shrink: read the schedule from F instead of a seed\n"
"  --save-schedule F   write the (reduced) schedule to F\n"
"  --stop-on-find      end the campaign at the first finding\n"
"  --json F            write the machine-readable report to F\n"
"  --quiet             suppress per-run lines\n"
"  --console           echo the guest's UART output (workloads say what failed;\n"
"                      inferring it from cycle counts wastes hours)\n"
"  --dump-regs         print every hart's GPRs when a run does not end `ok`\n"
"  --timeline F        write per-hart progress over time to F (CSV, for plots)\n"
"  --timeline-every N  sample the timeline every N cycles (default 200)\n"
"\n"
"Exit: 0 clean, 1 finding(s), 2 usage error.\n");
}

bool parse(int argc, char **argv, Options &o) {
  if (argc < 2) return false;
  o.mode = argv[1];
  if (o.mode != "run" && o.mode != "replay" && o.mode != "sweep" &&
      o.mode != "shrink")
    return false;
  for (int i = 2; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "kairos: %s needs a value\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if      (a == "--image")         o.spec.image = next();
    else if (a == "--dtb")           o.spec.dtb = next();
    else if (a == "--boot")          o.spec.boot = next();
    else if (a == "--policy")        o.policy = next();
    else if (a == "--policies")      o.policies = next();
    else if (a == "--runs")          o.runs = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--cycles")        o.spec.budget = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--seed")          o.seed = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--horizon")       o.horizon = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--hang")          o.spec.hang = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--livelock-factor") o.spec.livelock_factor = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--json")          o.json = next();
    else if (a == "--schedule")      o.schedule_in = next();
    else if (a == "--save-schedule") o.schedule_out = next();
    else if (a == "--shrink")        o.shrink = true;
    else if (a == "--shrink-trials") o.shrink_trials = atoi(next().c_str());
    else if (a == "--done-pc")       o.spec.done.pcs.push_back(strtoull(next().c_str(), nullptr, 0));
    else if (a == "--done-a0")     { o.spec.done.check_a0 = true;
                                     o.spec.done.a0 = strtoull(next().c_str(), nullptr, 0); }
    else if (a == "--stop-on-find")  o.stop_on_find = true;
    else if (a == "--quiet")         o.quiet = true;
    else if (a == "--console")       o.spec.console = true;
    else if (a == "--dump-regs")     o.dump_regs = true;
    else if (a == "--timeline")    { o.timeline = next();
                                     if (!o.spec.timeline_every) o.spec.timeline_every = 200; }
    else if (a == "--timeline-every") o.spec.timeline_every = strtoull(next().c_str(), nullptr, 0);
    else if (a == "-h" || a == "--help") return false;
    else { std::fprintf(stderr, "kairos: unknown option %s\n", a.c_str()); return false; }
  }
  if (o.spec.image.empty()) return false;

  // Argument checks belong HERE, before any simulation runs. Validating inside
  // a mode means the calibration run has already happened and the user reads
  // its output before the error that says their command was wrong.
  //
  // --policies is a sweep-mode flag. Silently running one policy when the
  // caller asked for four would report a comparison between arms that never
  // ran, so this is an error rather than a default.
  if (!o.policies.empty() && o.mode != "sweep") {
    std::fprintf(stderr,
                 "kairos: --policies is only meaningful for `kairos sweep`; "
                 "use `kairos sweep --policies %s` or `kairos %s --policy <one>`\n",
                 o.policies.c_str(), o.mode.c_str());
    std::exit(2);
  }
  if (!o.schedule_in.empty() && o.mode != "replay" && o.mode != "shrink") {
    std::fprintf(stderr,
                 "kairos: --schedule applies to `replay` and `shrink`, not `%s`\n",
                 o.mode.c_str());
    std::exit(2);
  }
  if (o.spec.budget == 0) {
    std::fprintf(stderr, "kairos: --cycles 0 would run nothing\n");
    std::exit(2);
  }
  return true;
}

// ── Reporting ───────────────────────────────────────────────────────────────

struct Record {
  std::string policy_spec, policy_config;
  uint64_t    seed = 0;
  RunResult   res;
  bool        shrunk = false;
  ShrinkStats shrink;
  std::string schedule_path;
};

// Where every hart ended up. Printed for any verdict that is not `ok`, because
// "the workload did not finish" is not actionable and "harts 1-3 are parked at
// 0x800008ac while hart 0 spins at 0x80000a10" is.
void print_hart_state(const RunResult &r, bool regs) {
  static const char *abi[33] = {
    "zero","ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1","a2","a3",
    "a4","a5","a6","a7","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
    "t3","t4","t5","t6","pc"};
  for (int h = 0; h < r.harts; h++) {
    std::printf("      hart %d  pc 0x%08" PRIx64 "  retired %" PRIu64 "\n",
                h, r.per_hart_pc[h], r.per_hart_retired[h]);
    if (!regs) continue;
    for (int g = 1; g < 32; g++) {
      std::printf("        %-4s %016" PRIx64 "%s", abi[g], r.gpr[h][g],
                  (g % 4 == 0) ? "\n" : "");
    }
    std::printf("\n");
  }
}

void print_run(const Options &o, const std::string &spec, uint64_t seed,
               const RunResult &r) {
  if (o.quiet) return;
  std::printf("  seed %-6" PRIu64 " %-13s cyc %-10" PRIu64 " retired %-9" PRIu64
              " digest %016" PRIx64 " %s%s%s\n",
              seed, verdict_name(r.verdict), r.cycles, r.retired_total,
              r.digest, r.fresh ? "NEW " : "seen",
              r.detail.empty() ? "" : "  ", r.detail.c_str());
  if (r.verdict != Verdict::Ok) print_hart_state(r, o.dump_regs);
  (void)spec;
}

// Re-run a seed with a Recorder so the exact schedule that produced a finding
// becomes a file. The policy is rebuilt from the same seed, so the recorded
// schedule is by construction the one the campaign just executed.
Schedule record_schedule(const Options &o, const std::string &spec, uint64_t seed) {
  Schedule sched;
  sched.image = o.spec.image;
  sched.policy_spec = spec;
  sched.seed = seed;
  auto inner = make_policy(spec, seed, ChironGeom::kHarts, o.horizon);
  Recorder rec(std::move(inner), &sched);
  run_once(o.spec, rec, nullptr);
  rec.finish();
  return sched;
}

// The shrink predicate: does this candidate schedule still reproduce the SAME
// failure? Not merely "still fails" -- reducing a hang into a different,
// easier wrong-result failure would silently retarget the reduction onto
// another bug and the reported minimal schedule would explain nothing.
bool same_failure(const RunResult &a, const RunResult &target) {
  if (a.verdict != target.verdict) return false;
  if (target.hart >= 0 && a.hart != target.hart) return false;
  return true;
}

Record shrink_finding(const Options &o, const std::string &spec, uint64_t seed,
                      const RunResult &target) {
  Record rec;
  rec.policy_spec = spec;
  rec.seed = seed;
  rec.res = target;

  Schedule sched = record_schedule(o, spec, seed);
  if (sched.empty()) {
    // Nothing was perturbed, so there is nothing to reduce: the failure is in
    // the design's own default schedule. Worth saying plainly -- it means the
    // bug reproduces without Kairos at all.
    std::printf("  (no perturbation in this run: the failure is present in the "
                "unperturbed schedule)\n");
    return rec;
  }

  std::printf("  shrinking: %zu span(s), %" PRIu64 " hart-cycles held\n",
              sched.size(), sched.stall_cycles());

  Shrinker sh(
      [&](Schedule &cand) {
        Replay rp(&cand);
        const RunResult r = run_once(o.spec, rp, nullptr);
        return same_failure(r, target);
      },
      !o.quiet, o.shrink_trials);

  rec.shrink = sh.run(sched);
  rec.shrunk = true;

  std::printf("  reduced: %zu -> %zu span(s), %" PRIu64 " -> %" PRIu64
              " hart-cycles, %d simulation run(s)%s\n",
              rec.shrink.spans_before, rec.shrink.spans_after,
              rec.shrink.cycles_before, rec.shrink.cycles_after,
              rec.shrink.trials,
              rec.shrink.budget_exhausted ? " [trial budget exhausted]" : "");
  for (const Span &s : sched.spans)
    for (int h = 0; h < ChironGeom::kHarts; h++)
      if (s.mask & (1u << h))
        std::printf("    hart %d held for %" PRIu64 " cycle(s) at %" PRIu64 "\n",
                    h, s.width(), s.start);

  std::string path = o.schedule_out;
  if (path.empty())
    path = "build/kairos-" + spec.substr(0, spec.find(':')) + "-" +
           std::to_string(seed) + ".ksched";
  if (sched.save(path)) {
    rec.schedule_path = path;
    std::printf("    schedule: %s  (kairos replay --image %s --schedule %s)\n",
                path.c_str(), o.spec.image.c_str(), path.c_str());
  }
  return rec;
}

void write_timeline(const Options &o, const RunResult &r) {
  if (o.timeline.empty() || r.timeline.empty()) return;
  FILE *f = std::fopen(o.timeline.c_str(), "w");
  if (!f) { std::fprintf(stderr, "kairos: cannot write %s\n", o.timeline.c_str()); return; }
  std::fprintf(f, "cycle,stall_mask");
  for (int h = 0; h < r.harts; h++) std::fprintf(f, ",retired%d", h);
  std::fprintf(f, "\n");
  for (const TimelinePoint &p : r.timeline) {
    std::fprintf(f, "%" PRIu64 ",%u", p.cycle, p.stall_mask);
    for (int h = 0; h < r.harts; h++) std::fprintf(f, ",%" PRIu64, p.retired[h]);
    std::fprintf(f, "\n");
  }
  std::fclose(f);
  std::printf("  timeline: %s (%zu samples)\n", o.timeline.c_str(), r.timeline.size());
}

void write_json(const Options &o, const std::vector<Record> &recs,
                const std::map<std::string, Coverage *> &covs) {
  if (o.json.empty()) return;
  FILE *f = std::fopen(o.json.c_str(), "w");
  if (!f) { std::fprintf(stderr, "kairos: cannot write %s\n", o.json.c_str()); return; }
  std::fprintf(f, "{\n  \"tool\": \"kairos\",\n  \"mode\": \"%s\",\n"
                  "  \"image\": \"%s\",\n  \"cycle_budget\": %" PRIu64 ",\n"
                  "  \"hang_threshold\": %" PRIu64 ",\n"
                  "  \"horizon\": %" PRIu64 ",\n"
                  "  \"baseline_cycles\": %" PRIu64 ",\n"
                  "  \"livelock_factor\": %" PRIu64 ",\n",
               o.mode.c_str(), o.spec.image.c_str(), o.spec.budget, o.spec.hang,
               o.horizon, o.spec.baseline, o.spec.livelock_factor);
  std::fprintf(f, "  \"policies\": {\n");
  bool first = true;
  for (const auto &kv : covs) {
    if (!first) std::fprintf(f, ",\n");
    first = false;
    std::fprintf(f,
      "    \"%s\": {\"runs\": %" PRIu64 ", \"distinct_schedules\": %" PRIu64
      ", \"distinct_order_pairs\": %" PRIu64 ", \"novelty_rate\": %.6f,"
      " \"events\": %" PRIu64 "}",
      kv.first.c_str(), kv.second->runs(), kv.second->distinct_schedules(),
      kv.second->distinct_order_pairs(), kv.second->novelty_rate(),
      kv.second->events());
  }
  std::fprintf(f, "\n  },\n  \"runs\": [\n");
  for (size_t i = 0; i < recs.size(); i++) {
    const Record &r = recs[i];
    std::fprintf(f,
      "    {\"policy\": \"%s\", \"config\": \"%s\", \"seed\": %" PRIu64
      ", \"cycles\": %" PRIu64 ", \"retired\": %" PRIu64
      ", \"digest\": \"%016" PRIx64 "\", \"fresh\": %s, \"verdict\": \"%s\""
      ", \"find_cycle\": %" PRIu64 ", \"hart\": %d, \"detail\": \"%s\"",
      r.policy_spec.c_str(), r.res.policy_config.c_str(), r.seed, r.res.cycles,
      r.res.retired_total, r.res.digest, r.res.fresh ? "true" : "false",
      verdict_name(r.res.verdict), r.res.find_cycle, r.res.hart,
      r.res.detail.c_str());
    if (r.shrunk)
      std::fprintf(f,
        ", \"shrink\": {\"spans_before\": %zu, \"spans_after\": %zu,"
        " \"cycles_before\": %" PRIu64 ", \"cycles_after\": %" PRIu64
        ", \"trials\": %d, \"budget_exhausted\": %s, \"schedule\": \"%s\"}",
        r.shrink.spans_before, r.shrink.spans_after, r.shrink.cycles_before,
        r.shrink.cycles_after, r.shrink.trials,
        r.shrink.budget_exhausted ? "true" : "false", r.schedule_path.c_str());
    if (r.shrunk && !r.shrink.trace.empty()) {
      std::fprintf(f, ", \"shrink_trace\": [");
      for (size_t t = 0; t < r.shrink.trace.size(); t++) {
        const ShrinkTrial &tr = r.shrink.trace[t];
        std::fprintf(f, "%s{\"i\":%d,\"phase\":%d,\"spans\":%zu,"
                        "\"cycles\":%" PRIu64 ",\"repro\":%s,\"accepted\":%s}",
                     t ? "," : "", tr.index, tr.phase, tr.spans, tr.cycles,
                     tr.reproduced ? "true" : "false",
                     tr.accepted ? "true" : "false");
      }
      std::fprintf(f, "]");
    }
    std::fprintf(f, "}%s\n", i + 1 == recs.size() ? "" : ",");
  }
  std::fprintf(f, "  ]\n}\n");
  std::fclose(f);
  std::printf("report: %s\n", o.json.c_str());
}

std::vector<std::string> split_commas(const std::string &s) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p <= s.size()) {
    size_t c = s.find(',', p);
    if (c == std::string::npos) c = s.size();
    if (c > p) out.push_back(s.substr(p, c - p));
    p = c + 1;
  }
  return out;
}

// ── Modes ───────────────────────────────────────────────────────────────────

// ── Calibration ─────────────────────────────────────────────────────────────
// A policy has to know WHEN to perturb. Spreading delays uniformly over the
// cycle budget is wrong whenever the budget is a safety cap rather than a
// prediction: a 6,600-cycle microbenchmark under a 400,000-cycle budget gets
// its one window placed after the program already finished, in 98% of runs, and
// the campaign reports a long row of identical digests while looking healthy.
// That bug is silent, so Kairos measures instead of assuming: one unperturbed
// run first, and the policies spread over the length it actually took.
//
// The calibration run is also the CONTROL. If the workload does not pass
// unperturbed, nothing found afterwards can be attributed to scheduling, and
// the campaign says so instead of collecting findings against a broken baseline.
bool calibrate(Options &o) {
  auto pol = make_policy("deterministic", 0, ChironGeom::kHarts, 0);
  const RunResult r = run_once(o.spec, *pol, nullptr);

  std::printf("baseline (unperturbed): %s after %" PRIu64 " cycles, "
              "%" PRIu64 " instructions retired\n",
              verdict_name(r.verdict), r.cycles, r.retired_total);

  if (is_finding(r.verdict)) {
    std::printf("kairos: the workload already fails WITHOUT perturbation "
                "(%s). Fix that first — nothing found under a schedule policy "
                "could be attributed to scheduling.\n", r.detail.c_str());
    return false;
  }
  if (r.verdict == Verdict::Timeout)
    std::printf("kairos: baseline did not complete within the budget; "
                "raise --cycles or the campaign measures only its own cap.\n");

  if (!o.horizon) {
    o.horizon = r.cycles;
    std::printf("horizon: %" PRIu64 " cycles (measured)\n", o.horizon);
  } else {
    std::printf("horizon: %" PRIu64 " cycles (given)\n", o.horizon);
  }

  // The baseline arms the livelock layer, and it also lets the campaign size
  // its own budget. Left at the default 2,000,000 a 6,600-cycle workload spends
  // 300x its own length proving nothing, and every shrink trial pays it again.
  o.spec.baseline = r.cycles;
  if (o.spec.livelock_factor && r.cycles) {
    const uint64_t needed = r.cycles * o.spec.livelock_factor + r.cycles;
    if (o.spec.budget > needed) {
      std::printf("budget: %" PRIu64 " -> %" PRIu64 " cycles "
                  "(%" PRIu64 "x the unperturbed length; past that it is a "
                  "livelock, not a longer run)\n",
                  o.spec.budget, needed, o.spec.livelock_factor);
      o.spec.budget = needed;
    } else if (o.spec.budget < needed) {
      std::printf("note: --cycles %" PRIu64 " is below the livelock threshold "
                  "%" PRIu64 "; slow schedules will be reported as 'timeout' "
                  "rather than judged\n", o.spec.budget, needed);
    }
  }
  return true;
}

int mode_explore(Options &o) {
  if (!calibrate(o)) return 2;

  std::vector<std::string> specs =
      (o.mode == "sweep" && !o.policies.empty()) ? split_commas(o.policies)
                                                 : std::vector<std::string>{o.policy};
  std::vector<Record> recs;
  std::map<std::string, Coverage *> covs;
  int findings = 0;

  for (const std::string &spec : specs) {
    Coverage *cov = new Coverage(ChironGeom::kHarts);
    covs[spec] = cov;
    std::printf("\n=== policy %s | %" PRIu64 " run(s) | %" PRIu64 " cycle budget ===\n",
                spec.c_str(), o.runs, o.spec.budget);

    for (uint64_t i = 0; i < o.runs; i++) {
      const uint64_t seed = o.seed + i;
      auto pol = make_policy(spec, seed, ChironGeom::kHarts, o.horizon);
      const RunResult r = run_once(o.spec, *pol, cov);

      Record rec;
      rec.policy_spec = spec;
      rec.seed = seed;
      rec.res = r;
      print_run(o, spec, seed, r);

      if (is_finding(r.verdict)) {
        findings++;
        std::printf("  >>> FINDING (%s) at cycle %" PRIu64 "\n"
                    "      reproduce: kairos replay --image %s --policy %s --seed %" PRIu64 "\n",
                    verdict_name(r.verdict), r.find_cycle, o.spec.image.c_str(),
                    spec.c_str(), seed);
        if (o.shrink) rec = shrink_finding(o, spec, seed, r);
      }
      recs.push_back(rec);
      if (findings && o.stop_on_find) goto done;
    }

    std::printf("  --- %-14s %" PRIu64 "/%" PRIu64 " distinct schedules, "
                "%" PRIu64 " order pairs, novelty %.3f\n",
                spec.c_str(), cov->distinct_schedules(), cov->runs(),
                cov->distinct_order_pairs(), cov->novelty_rate());
  }

done:
  std::printf("\nkairos: %zu run(s), %d finding(s)\n", recs.size(), findings);
  write_json(o, recs, covs);
  for (auto &kv : covs) delete kv.second;
  return findings ? 1 : 0;
}

int mode_replay(Options &o) {
  Schedule sched;
  std::unique_ptr<Policy> pol;

  if (!o.schedule_in.empty()) {
    if (!sched.load(o.schedule_in)) {
      std::fprintf(stderr, "kairos: cannot read schedule %s\n", o.schedule_in.c_str());
      return 2;
    }
    std::printf("replaying %s: %zu span(s), %" PRIu64 " hart-cycles held\n",
                o.schedule_in.c_str(), sched.size(), sched.stall_cycles());
    pol.reset(new Replay(&sched));
  } else {
    std::printf("replaying policy %s seed %" PRIu64 "\n", o.policy.c_str(), o.seed);
    if (!o.horizon) calibrate(o);
    pol = make_policy(o.policy, o.seed, ChironGeom::kHarts, o.horizon);
  }

  Coverage cov(ChironGeom::kHarts);
  const RunResult r = run_once(o.spec, *pol, &cov);
  std::printf("  verdict %s at cycle %" PRIu64 " (ran %" PRIu64 " cycles, "
              "%" PRIu64 " instructions retired)\n    %s\n",
              verdict_name(r.verdict), r.find_cycle, r.cycles, r.retired_total,
              r.detail.c_str());
  std::printf("  schedule digest %016" PRIx64 "\n", r.digest);
  print_hart_state(r, o.dump_regs);
  write_timeline(o, r);

  if (!o.schedule_out.empty() && o.schedule_in.empty()) {
    Schedule rec = record_schedule(o, o.policy, o.seed);
    if (rec.save(o.schedule_out))
      std::printf("  schedule written to %s\n", o.schedule_out.c_str());
  }
  return is_finding(r.verdict) ? 1 : 0;
}

int mode_shrink(Options &o) {
  Schedule sched;
  RunResult target;

  if (!o.schedule_in.empty()) {
    if (!sched.load(o.schedule_in)) {
      std::fprintf(stderr, "kairos: cannot read schedule %s\n", o.schedule_in.c_str());
      return 2;
    }
    Replay rp(&sched);
    target = run_once(o.spec, rp, nullptr);
  } else {
    if (!o.horizon) calibrate(o);
    auto pol = make_policy(o.policy, o.seed, ChironGeom::kHarts, o.horizon);
    target = run_once(o.spec, *pol, nullptr);
  }

  if (!is_finding(target.verdict)) {
    std::printf("kairos: nothing to shrink — this schedule ends in '%s', "
                "not a finding\n", verdict_name(target.verdict));
    return 0;
  }
  std::printf("target: %s at cycle %" PRIu64 " (hart %d)\n    %s\n",
              verdict_name(target.verdict), target.find_cycle, target.hart,
              target.detail.c_str());
  print_hart_state(target, o.dump_regs);

  Options oo = o;
  if (oo.schedule_out.empty())
    oo.schedule_out = "build/kairos-min.ksched";
  const Record rec = shrink_finding(oo, o.policy, o.seed, target);
  std::vector<Record> recs{rec};
  std::map<std::string, Coverage *> none;
  write_json(o, recs, none);
  return 1;
}

}  // namespace

int main(int argc, char **argv) {
  // Line-buffer stdout. A campaign is a long-running job that people redirect
  // to a file or a CI log, and block buffering means nothing appears for
  // minutes -- which is indistinguishable from a hung tool, and the first thing
  // anyone does about it is kill the run.
  setvbuf(stdout, nullptr, _IOLBF, 0);

  Options o;
  if (!parse(argc, argv, o)) { usage(); return 2; }
  if (o.mode == "replay") return mode_replay(o);
  if (o.mode == "shrink") return mode_shrink(o);
  return mode_explore(o);
}
