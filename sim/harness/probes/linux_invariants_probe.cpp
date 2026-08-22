// linux_invariants_probe.cpp — run ci-check's per-cycle microarchitectural
// assertions against the LINUX BOOT, not just the benchmarks.
//
// `make ci-check` builds profile_quad.cpp with -DCHIRON_INVARIANTS and gates on
// five short quad-core benchmarks. Those workloads are tight numeric kernels:
// they exercise almost no traps, no CSR work, no cross-hart IPI, and their
// speculation patterns are nothing like a kernel's. The defect class the
// invariants exist to catch (a completion landing on a rolled-back and
// REALLOCATED ROB slot) fires roughly once in tens of millions of cycles, so
// the boot is where it actually lives.
//
// This boots the real image from reset with the same observers attached and
// the same PRE-EDGE sampling rule, and reports every gate at the end:
//   * branch-readied-without-resolution
//   * ready-outside-ROB-window
//   * double-resolve
//   * wedged (no retire for CHIRON_WEDGE_CYCLES)
//
// Exit status is nonzero if any gate tripped, so it can be a CI gate too.
//
// Build: make build/linux_invariants_probe.out
// Run  : END=40000000 ./build/linux_invariants_probe.out          # from reset
//        (checkpoints in ckpt_old/ predate the 32-entry ROB / 32 KB I$ and
//         CANNOT be restored into this model -- the Verilated register layout
//         changed. Boot from reset.)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"
#include "sim/harness/invariants.h"

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";
  auto env64 = [](const char *n, uint64_t d) {
    const char *v = getenv(n); return v ? strtoull(v, nullptr, 0) : d;
  };
  const uint64_t END    = env64("END", 40000000ULL);
  const uint64_t EVERY  = env64("EVERY", 5000000ULL);

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb = bench.raw();

  chiron_invariants inv;
  inv.attach(tb);

  std::fprintf(stderr, "[linux-inv] booting %s for %llu cycles with"
               " per-cycle invariants\n", image, (unsigned long long)END);

  for (uint64_t cyc = 0; cyc < END; ++cyc) {
    tb->eval();
    inv.tick(cyc);                 // PRE-EDGE: see invariants.h sampling rules
    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    if (EVERY && cyc && cyc % EVERY == 0)
      std::fprintf(stderr, "[linux-inv] %llu cycles, %llu violations so far\n",
                   (unsigned long long)cyc,
                   (unsigned long long)inv.violations());
  }
  return inv.report();
}
