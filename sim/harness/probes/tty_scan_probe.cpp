// tty_scan_probe.cpp — boot linux-q4 RTL-only and periodically grep the DRAM
// backing store for console text that SHOULD have been printed but never
// reached the UART (post-/init silent-console failure). If the missing
// strings ("mounting sysfs", "Starting syslogd", ...) appear in DRAM, the
// tty layer accepted the writes and they died between the tty ring and the
// uartlite TX drain; if they never appear, the writes died higher up
// (ldisc/mutex/scheduler). Also mirrors the uart TX edge-detect so scan hits
// can be correlated with real console output.
//
// Build:  make build/tty_scan_probe.out
// Env  :  TTYSCAN_END (default 900M), TTYSCAN_FROM (820M), TTYSCAN_STEP (20M)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

static const char *needles[] = {
    "mounting sysfs",
    "mounting devpts",
    "can't read '/proc/mounts'",
    "Starting syslogd",
    "Starting network",
    "Welcome to Buildroot",
    "buildroot login",
    "seedrng",
};
static const int n_needles = sizeof(needles) / sizeof(needles[0]);

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("TTYSCAN_END",  900000000ULL);
  const uint64_t SCAN_FROM = env64("TTYSCAN_FROM", 820000000ULL);
  const uint64_t SCAN_STEP = env64("TTYSCAN_STEP",  20000000ULL);

  const uint8_t *dram =
      (const uint8_t *)&tb_->system__DOT__memory__DOT__memory;
  const size_t dram_sz = sizeof(tb_->system__DOT__memory__DOT__memory);

  bool tx_prev[4] = {false, false, false, false};
  char linebuf[512]; int linelen = 0;
  uint64_t cyc = 0, next_scan = SCAN_FROM;

  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;

    const bool tx_v[4] = {tb_->core0OutChar_valid != 0, tb_->core1OutChar_valid != 0,
                          tb_->core2OutChar_valid != 0, tb_->core3OutChar_valid != 0};
    const char tx_b[4] = {(char)tb_->core0OutChar_byte, (char)tb_->core1OutChar_byte,
                          (char)tb_->core2OutChar_byte, (char)tb_->core3OutChar_byte};
    for (int p = 0; p < 4; ++p) {
      if (tx_v[p] && !tx_prev[p]) {
        char c = tx_b[p];
        if (c == '\n' || linelen >= (int)sizeof(linebuf) - 2) {
          linebuf[linelen] = 0;
          printf("[con %9llu] %s\n", (unsigned long long)cyc, linebuf);
          fflush(stdout);
          linelen = 0;
        } else if (c != '\r') {
          linebuf[linelen++] = c;
        }
      }
      tx_prev[p] = tx_v[p];
    }

    if ((cyc % 20000000ULL) == 0) {
      printf("[prog %9llu] pcs=%llx/%llx/%llx/%llx\n", (unsigned long long)cyc,
             (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
             (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
      fflush(stdout);
    }

    if (cyc < next_scan) continue;
    next_scan += SCAN_STEP;

    printf("[scan %9llu] begin\n", (unsigned long long)cyc);
    for (int n = 0; n < n_needles; ++n) {
      const char *nd = needles[n];
      const size_t nl = strlen(nd);
      int hits = 0;
      for (size_t off = 0; off + nl < dram_sz; ++off) {
        if (dram[off] == nd[0] && memcmp(dram + off, nd, nl) == 0) {
          printf("[scan]   \"%s\" @ 0x%llx", nd,
                 (unsigned long long)(0x80000000ULL + off));
          // context: 96 printable-ish bytes around the hit
          printf("  ctx=\"");
          for (size_t k = (off > 32 ? off - 32 : 0);
               k < off + 64 && k < dram_sz; ++k) {
            char c = (char)dram[k];
            putchar((c >= 32 && c < 127) ? c : '.');
          }
          printf("\"\n");
          if (++hits >= 4) { printf("[scan]   (more hits suppressed)\n"); break; }
          off += nl;
        }
      }
      if (!hits) printf("[scan]   \"%s\" NOT FOUND\n", nd);
    }
    fflush(stdout);
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
