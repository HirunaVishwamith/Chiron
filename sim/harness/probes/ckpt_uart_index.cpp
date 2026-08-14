// ckpt_uart_index — map each checkpoint to how much console output had been
// emitted by that cycle.
//
// Locating a symptom in a 1.5-billion-cycle boot by re-simulating is hours of
// work per guess. But every checkpoint's header already carries the running
// UART byte count, and the boot log is exactly that byte stream — so the offset
// of any console line in the log can be turned straight into a bracketing pair
// of checkpoints, in seconds and with no simulation at all.
//
// The header layout is fixed by the probes that write it (linux_ipi_probe and
// friends): cycle, msip[4], last_uart, uart_bytes, then the whole model. Only
// the leading scalars are read here; the model itself is skipped, which is what
// makes this cheap.
//
//   ./build/ckpt_uart_index.out ckpt/*.bin

#include <cinttypes>
#include <cstdio>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

int main(int argc, char **argv) {
  std::printf("%14s  %12s  %s\n", "cycle", "uart_bytes", "checkpoint");
  for (int i = 1; i < argc; i++) {
    uint64_t cyc = 0, msip[4] = {}, last_uart = 0, uart_bytes = 0;
    // Deliberately heap-allocated and never closed/freed: reading only the
    // leading scalars leaves the stream mid-file, and both close() and the
    // destructor abort on the missing end-of-file signature. Stopping early is
    // the whole point of this tool, so the stream is simply abandoned.
    VerilatedRestore *rs = new VerilatedRestore();
    rs->open(argv[i]);
    if (!rs->isOpen()) { std::fprintf(stderr, "cannot open %s\n", argv[i]); continue; }
    *rs >> cyc; *rs >> msip[0]; *rs >> msip[1]; *rs >> msip[2]; *rs >> msip[3];
    *rs >> last_uart; *rs >> uart_bytes;
    std::printf("%14" PRIu64 "  %12" PRIu64 "  %s\n", cyc, uart_bytes, argv[i]);
  }
  return 0;
}
