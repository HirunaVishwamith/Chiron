// linux_itext_probe.cpp — does DRAM actually contain code where hart0 fetched
// zeros?
//
// 2026-08-12. The 1,235M Linux freeze was traced to its real cause: hart0's ROB
// fills with 16 consecutive entries whose PCs are perfectly sequential from
// 0x81b03840 but whose INSTRUCTION WORDS ARE ALL 0x00000000 (linux_robjam_probe
// dump). An all-zero word is an illegal RISC-V instruction — it can never issue,
// so the scheduler sits empty (occupied=0), every functional unit reads idle,
// and the ROB head never completes. Not a lost completion; nothing was ever
// executable.
//
// 0x81b03840 is in the initramfs/userspace region: memory the kernel WRITES as
// data during unpack and then EXECUTES, which is exactly the I-cache/D-cache
// coherence boundary that fence.i exists to cover.
//
// This restores a checkpoint and reads DRAM directly — no simulation, so it runs
// in seconds rather than the ~18 minutes a run-to-the-wedge probe costs.
//
//   DRAM holds real instructions -> I-fetch returned stale zeros; the I-cache
//                                   never saw the kernel's stores (fence.i /
//                                   I-cache coherence bug).
//   DRAM holds zeros             -> AMBIGUOUS, and read_dram64 is why: it sees
//                                   DRAM only, never dirty L1/L2 lines. So this
//                                   means EITHER the region is genuinely empty
//                                   (a bad jump) OR the unpacked code is still
//                                   dirty in some hart's D-cache and was never
//                                   written back — which is still the coherence
//                                   story, just from the other side.
//
// Build: make build/linux_itext_probe.out
// Run:   CKPT_RESTORE=ckpt_prefix3_dualUnique_only/ckpt_001200000000.bin \
//          IT_ADDR=0x81b03840 ./build/linux_itext_probe.out
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

int main() {
  const char *ckpt = getenv("CKPT_RESTORE");
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }
  const uint64_t ADDR = getenv("IT_ADDR") ? strtoull(getenv("IT_ADDR"), 0, 0)
                                          : 0x81b03840ULL;
  const uint64_t WORDS = getenv("IT_WORDS") ? strtoull(getenv("IT_WORDS"), 0, 0)
                                            : 24ULL;

  simulator bench;
  bench.init_no_image();
  Vsystem *tb = bench.raw();

  uint64_t cyc = 0, msip_writes[4] = {}, last_uart_cyc = 0, uart_bytes = 0;
  {
    VerilatedRestore rs;
    rs.open(ckpt);
    if (!rs.isOpen()) { std::fprintf(stderr, "cannot open %s\n", ckpt); return 1; }
    rs >> cyc;
    rs >> msip_writes[0]; rs >> msip_writes[1];
    rs >> msip_writes[2]; rs >> msip_writes[3];
    rs >> last_uart_cyc;  rs >> uart_bytes;
    rs >> *tb; rs.close();
  }
  std::printf("restored at cycle %llu (no simulation)\n\n",
              (unsigned long long)cyc);

  // SCAN mode: sample one word every IT_STRIDE bytes across a range and report
  // only which chunks hold ANY non-zero data. Answers "where does real content
  // live in DRAM at all?" -- needed because the userspace text turned out to be
  // dirty in the cache hierarchy with DRAM never updated, so a single all-zero
  // read proves nothing on its own.
  if (getenv("IT_SCAN")) {
    const uint64_t lo     = ADDR;
    const uint64_t hi     = getenv("IT_HI") ? strtoull(getenv("IT_HI"), 0, 0)
                                            : ADDR + 0x400000ULL;
    const uint64_t stride = getenv("IT_STRIDE") ? strtoull(getenv("IT_STRIDE"), 0, 0)
                                                : 0x1000ULL;
    std::printf("SCAN %llx .. %llx stride %llu: chunks containing non-zero data\n",
                (unsigned long long)lo, (unsigned long long)hi,
                (unsigned long long)stride);
    uint64_t chunks = 0, hits = 0;
    for (uint64_t a = lo; a < hi; a += stride) {
      bool any = false;
      // Sample a few words inside each chunk rather than only the first, so a
      // sparsely-populated page is not missed.
      for (uint64_t o = 0; o < stride && o < 256; o += 8)
        if (bench.read_dram64((a + o) & ~7ULL)) { any = true; break; }
      chunks++;
      if (any) { hits++; if (hits <= 40) std::printf("  %016llx  NON-ZERO\n",
                                                     (unsigned long long)a); }
    }
    std::printf("\n%llu / %llu chunks hold data\n",
                (unsigned long long)hits, (unsigned long long)chunks);
    return 0;
  }

  // Start a little before the fetch address so the surrounding region is
  // visible: an abrupt zero/non-zero boundary is itself informative.
  const uint64_t base = (ADDR >= 32) ? (ADDR - 32) : ADDR;
  std::printf("DRAM around %llx (32 bytes before .. %llu words):\n",
              (unsigned long long)ADDR, (unsigned long long)WORDS);
  std::printf("%-18s %-10s %s\n", "addr", "insn", "note");

  unsigned nonzero = 0, zero = 0, legal = 0;
  for (uint64_t i = 0; i < WORDS; ++i) {
    const uint64_t a = base + i * 4;
    const uint64_t dw = bench.read_dram64(a & ~7ULL);
    const uint32_t w = (a & 4) ? (uint32_t)(dw >> 32) : (uint32_t)dw;
    // Every 32-bit RISC-V instruction has bits[1:0]==11; anything else is either
    // a 16-bit compressed insn (not enabled here) or not an instruction at all.
    const bool looksLegal = ((w & 3) == 3) && w != 0;
    if (w == 0) zero++; else nonzero++;
    if (looksLegal) legal++;
    std::printf("%016llx   %08x   %s%s\n", (unsigned long long)a, w,
                looksLegal ? "legal-looking insn" : (w == 0 ? "ZERO" : "not an insn"),
                (a == ADDR) ? "   <== hart0 fetched here" : "");
  }

  std::printf("\nsummary: %u zero, %u non-zero, %u legal-looking\n",
              zero, nonzero, legal);
  if (legal > 0) {
    std::printf("=> DRAM HOLDS CODE. The core fetched zeros from an address that\n");
    std::printf("   really does contain instructions => I-FETCH/I-CACHE returned\n");
    std::printf("   stale data (kernel stores not visible to instruction fetch).\n");
  } else {
    std::printf("=> DRAM holds no code here. AMBIGUOUS by construction:\n");
    std::printf("   read_dram64 cannot see dirty L1/L2 lines, so the unpacked\n");
    std::printf("   text may exist only in a D-cache and never have been written\n");
    std::printf("   back -- still a coherence failure. Distinguish by dumping the\n");
    std::printf("   D-cache tags for this line (linux_dcache_probe DC_ADDR).\n");
  }
  return 0;
}
