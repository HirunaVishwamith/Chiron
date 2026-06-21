// completion.h — shared "is the program done?" logic.
//
// Two orthogonal termination signals every driver understands:
//
//   * Benchmark completion — the caller passes one or more committed PCs via
//     --done-pc (optionally gated on a0 == --done-a0). The completion-PC values
//     live in the makefile benchmark manifest (mk/benchmarks.mk), so harnesses
//     stay benchmark-agnostic.
//
//   * RISC-V ISA test result — the riscv-tests harness exits via `ecall` with
//     a7 (x17) == 93; gp (x3) == 1 means pass, anything else means fail.
#pragma once

#include <stdint.h>
#include <vector>

#include "args.h"

namespace harness {

// Caller-supplied benchmark completion condition, parsed from argv.
struct Completion {
  std::vector<uint64_t> done_pcs;  // any of these committed PCs ...
  bool     have_a0 = false;        // ... optionally gated on ...
  uint64_t a0_val  = 0;            // ... a0 == a0_val.

  static Completion parse(int argc, char **argv) {
    Completion c;
    c.done_pcs = collect_hex_args(argc, argv, "--done-pc");
    const char *a0 = find_arg(argc, argv, "--done-a0", nullptr);
    c.have_a0 = (a0 != nullptr);
    c.a0_val  = c.have_a0 ? std::strtoull(a0, nullptr, 0) : 0;
    return c;
  }

  bool active() const { return !done_pcs.empty(); }

  // True when `pc` is a completion PC and the optional a0 gate is satisfied.
  bool hit(uint64_t pc, uint64_t a0) const {
    for (uint64_t dpc : done_pcs)
      if (pc == dpc) return !have_a0 || a0 == a0_val;
    return false;
  }
};

enum class IsaResult { Running, Pass, Fail };

// Classify an ISA-test exit from the committed gp/a7 register values.
inline IsaResult isa_test_status(uint64_t gp, uint64_t a7) {
  if (a7 != 93) return IsaResult::Running;
  return gp == 1 ? IsaResult::Pass : IsaResult::Fail;
}

}  // namespace harness
