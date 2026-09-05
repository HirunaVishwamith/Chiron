// kairos/dut.h — the portable boundary between Kairos and a design under test.
//
// Kairos explores SCHEDULES, not programs. Everything above this header (the
// policies, the coverage model, the oracle, the campaign driver) is design
// independent; everything below it is the ~200 lines you write to bind Kairos
// to a particular core. Porting Kairos to BOOM, CVA6, XiangShan or your own
// design means implementing this interface and nothing else.
//
// A DUT must be able to do four things:
//
//   1. RUN one cycle.
//   2. STALL a hart -- delay it, never force it forward. This is the whole
//      perturbation mechanism, and the reason Kairos cannot produce false
//      positives: a stall is indistinguishable from an ordinary backpressure
//      the design already generates for itself (an I-cache miss, an arbiter
//      loss, a full queue), so every schedule Kairos induces is one the
//      unmodified design could reach on its own.
//   3. OBSERVE the events that define an interleaving -- at minimum instruction
//      retirement; ideally coherence transactions too, which is what makes the
//      coverage model meaningful for a multicore.
//   4. REPORT liveness and completion, so the oracle can tell "finished",
//      "still working" and "wedged" apart.
//
// Anything a DUT cannot supply is declared unsupported rather than faked. A
// coverage number computed from events the DUT does not actually observe would
// be worse than no number at all.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kairos {

// One observable event in the interleaving. Kairos never interprets `detail`;
// it only needs events to be comparable and orderable so that two runs can be
// told apart.
struct Event {
  enum Kind : uint8_t {
    Retire,        // a hart committed an instruction
    CoherenceReq,  // a hart issued a coherence transaction
    CoherenceResp, // a coherence transaction completed
    StateChange,   // cache line changed ownership/state
  };
  Kind     kind;
  uint8_t  hart;
  uint64_t addr;    // line address where meaningful, else 0
  uint64_t detail;  // opaque: PC for Retire, txn type for coherence, etc.
};

// What a DUT can actually observe. Kairos degrades cleanly rather than
// pretending: a DUT with only `retire` still supports schedule exploration and
// the liveness oracle, but its coverage model is necessarily coarser, and the
// campaign report says so instead of quietly reporting a weaker number as if it
// were the strong one.
struct DutCapabilities {
  bool retire          = false;  // can report per-hart commits
  bool coherence       = false;  // can report coherence transactions
  bool line_state      = false;  // can report per-hart cache line state (SWMR)
  bool architectural   = false;  // has a golden model for differential checking
  int  harts           = 0;
};

class Dut {
 public:
  virtual ~Dut() = default;

  // ── Identity ─────────────────────────────────────────────────────────────
  virtual std::string name() const = 0;
  virtual DutCapabilities capabilities() const = 0;

  // ── Lifecycle ────────────────────────────────────────────────────────────
  // Load `image` and reset. Must leave the DUT at cycle 0 in a state that is
  // IDENTICAL for identical arguments -- Kairos's reproducibility guarantee
  // rests on this. If a DUT cannot promise that, it cannot be replayed, and
  // Kairos will report findings it cannot reproduce, which is worthless.
  virtual void reset(const std::string &image) = 0;

  // Advance exactly one cycle.
  virtual void step() = 0;

  virtual uint64_t cycle() const = 0;

  // ── Perturbation ─────────────────────────────────────────────────────────
  // Bit h set => hart h is stalled for the NEXT cycle.
  //
  // Contract, and it is the load-bearing one: this may only DELAY a hart. An
  // implementation that uses it to skip work, force progress, alter arbitration
  // outcomes, or change any value breaks Kairos's soundness argument and makes
  // every result it produces unciteable. If your design has no such signal, add
  // one that gates an existing ready/valid handshake -- do not synthesise a new
  // behaviour.
  virtual void set_stall(uint32_t hart_mask) = 0;
  virtual uint32_t max_stall_mask() const = 0;

  // ── Observation ──────────────────────────────────────────────────────────
  // Events that occurred during the cycle just stepped. Cleared each step.
  virtual const std::vector<Event> &events() const = 0;

  // Retired-instruction count per hart, for liveness and progress.
  virtual uint64_t retired(int hart) const = 0;

  // ── Completion ───────────────────────────────────────────────────────────
  // True once the workload has finished. A DUT with no notion of completion
  // returns false forever and the campaign relies on its cycle budget.
  virtual bool finished() const = 0;
  // Meaningful only when finished(): did the workload produce the right answer?
  virtual bool passed() const = 0;

  // ── Optional: where each hart currently is ───────────────────────────────
  // The PC of the last instruction hart `h` committed. A livelock verdict is a
  // suspicion until you know WHERE the harts are spinning, and this is the
  // cheapest possible answer: two spin loops in the same function is a slow
  // schedule, two harts parked on opposite sides of a publish/observe protocol
  // is a visibility failure. Returns 0 if the DUT cannot report it.
  virtual uint64_t last_pc(int hart) const { (void)hart; return 0; }

  // ── Optional: guest console ──────────────────────────────────────────────
  // Workloads say what happened. Ignoring that and inferring from cycle counts
  // is how a triage run burns 40 minutes and still cannot tell "failed" from
  // "slow" -- which is exactly what happened here before this existed.
  virtual void set_console(bool on) { (void)on; }

  // ── Optional: architectural register file ────────────────────────────────
  // GPR `r` of hart `h`. A wedge is usually explained by two registers: the
  // value a hart is waiting FOR and the value it is actually SEEING. Returns 0
  // if the DUT cannot report it.
  virtual uint64_t gpr(int hart, int r) const { (void)hart; (void)r; return 0; }

  // ── Optional: coherence state, for the SWMR/DVI oracle layer ─────────────
  // Returns false if unsupported. `owners` is filled with, for each (set,tag)
  // line currently held valid-and-not-shared by more than one hart, a packed
  // description. An empty vector means the invariant holds this cycle.
  virtual bool check_coherence(std::vector<std::string> &violations) const {
    (void)violations;
    return false;  // unsupported by default; capabilities() says so
  }
};

}  // namespace kairos
