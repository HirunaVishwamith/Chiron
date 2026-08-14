// uartrx_test — prove the console RX path works before spending 14 h on it.
//
// The quad-core Linux boot reaches "buildroot login: " and then needs the user
// to type `root` and `nproc`. Every part of that is new and untested: the
// uartlite RX registers in quard_uart.scala, the top-level hostInput port in
// system.scala, and linux_sim.cpp's stdin forwarding. Discovering a mistake in
// any of them at the login prompt, 14 hours in, is the expensive way to find
// out.
//
// This drives the SAME pins linux_sim.cpp drives (hostInput_valid/char, retired
// by hostInputConsumed) while running bins/mt-uartrx-q4.bin, which reads bytes
// off the uartlite RX FIFO and echoes them between '<' and '>'. If the echoed
// payload matches what was sent, the round trip
//
//     host -> hostInput -> AXI read of +0x08/+0x00 -> core -> TX -> host
//
// is intact and in order. It runs in seconds.
//
// Note this covers the hostInput path only, not uartPort's compiled-in ROM: the
// ROM stays locked until terminalReady has seen a login prompt, which no
// bare-metal program produces. The ROM is the fallback for an idle stdin; the
// path exercised here is the one that carries your keystrokes.
//
//   usage: uartrx_test.out [image.bin] [dtb] [bootrom]
//   env:   RX_TEXT   string to send (default "Ab3!xY z_9\n", must match
//                    RX_CHARS the .bin was built with)
//          RX_MAXCYC cycle cap (default 5,000,000)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sim/rtl/rtl_model.h"

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-uartrx-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  // Deliberately mixed case, digits, punctuation and a space: a path that
  // truncates, drops the top bits, or mangles a byte should not be able to pass
  // by accident the way an all-'a' string would let it.
  const std::string text = getenv("RX_TEXT") ? getenv("RX_TEXT") : "Ab3!xY z_9\n";
  const uint64_t maxcyc =
      getenv("RX_MAXCYC") ? strtoull(getenv("RX_MAXCYC"), nullptr, 0) : 5000000ULL;

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb = bench.raw();

  tb->hostInput_valid = 0;
  tb->hostInput_char  = 0;

  size_t sent = 0;
  std::string tx;
  bool tx_prev[4] = {false, false, false, false};
  uint64_t cyc = 0;

  std::printf("\n== uartrx_test ==\n");
  std::printf("   image: %s\n", image);
  std::printf("   sending %zu bytes: \"", text.size());
  for (char c : text) {
    if (c == '\n') std::printf("\\n"); else std::putchar(c);
  }
  std::printf("\"\n\n");
  std::fflush(stdout);

  const bool verbose     = getenv("RX_VERBOSE") != nullptr;
  const uint64_t poll = getenv("RX_POLL") ? strtoull(getenv("RX_POLL"), nullptr, 0) : 1ULL;
  uint64_t last_progress = 0;

  for (; cyc < maxcyc; ++cyc) {
    tb->eval();

    // Four-phase handshake against MultiUart's hostTaken latch:
    //   1. host raises valid with a byte
    //   2. a port pops it, hostTaken latches, consumed goes high
    //   3. host drops valid   -> hostTaken clears, consumed goes low
    //   4. host raises valid with the NEXT byte
    //
    // Step 3 is not optional and must span a clock edge. Re-raising valid in the
    // same iteration that observed consumed leaves valid high at every edge, so
    // hostTaken never clears, consumed stays stuck high, and the host retires
    // the whole string in consecutive cycles while the core reads none of it.
    // RX_POLL emulates linux_sim.cpp's cadence. That loop advances
    // commit-to-commit and only touches the console pins once per iteration, so
    // the handshake is serviced far more slowly than the clock. Clocking stays
    // cycle-accurate here (TX bytes are short pulses and would otherwise be
    // missed by the capture below — an artefact of the harness, not the RTL);
    // only the handshake is throttled, which is the property under test.
    const bool service = (cyc % poll) == 0;
    if (service && tb->hostInput_valid) {
      if (tb->hostInputConsumed) {
        if (verbose)
          std::printf("[%8llu] consumed '%c' (%zu/%zu)\n",
                      (unsigned long long)cyc,
                      text[sent] == '\n' ? '.' : text[sent], sent + 1, text.size());
        tb->hostInput_valid = 0;
        ++sent;
        last_progress = cyc;
      }
    } else if (service && !tb->hostInputConsumed && sent < text.size()) {
      tb->hostInput_char  = (unsigned char)text[sent];
      tb->hostInput_valid = 1;
    }

    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();

    if (verbose && (cyc % 200000 == 0) && cyc)
      std::printf("[%8llu] sent=%zu tx=%zu  pc0=%08lx pc1=%08lx\n",
                  (unsigned long long)cyc, sent, tx.size(),
                  (unsigned long)bench.core_pc(0), (unsigned long)bench.core_pc(1));

    // Edge-detect each port: valid is held high while the write is buffered.
    const bool v[4] = {tb->core0OutChar_valid != 0, tb->core1OutChar_valid != 0,
                       tb->core2OutChar_valid != 0, tb->core3OutChar_valid != 0};
    const char b[4] = {(char)tb->core0OutChar_byte, (char)tb->core1OutChar_byte,
                       (char)tb->core2OutChar_byte, (char)tb->core3OutChar_byte};
    for (int p = 0; p < 4; ++p) {
      if (v[p] && !tx_prev[p]) tx += b[p];
      tx_prev[p] = v[p];
    }

    if (tx.find('>') != std::string::npos) break;
    if (tx.find("RXFAIL") != std::string::npos) break;
  }

  (void)last_progress;
  std::printf("cycles: %llu   bytes accepted by the core: %zu / %zu\n",
              (unsigned long long)cyc, sent, text.size());
  std::printf("TX seen: \"");
  for (char c : tx) {
    if (c == '\n') std::printf("\\n"); else std::putchar(c);
  }
  std::printf("\"\n");

  if (tx.find("RXFAIL") != std::string::npos) {
    std::printf("\nFAIL: the program timed out polling STATUS for RX_VALID —\n"
                "      the RX_VALID bit at +0x08 is never asserting.\n");
    return 1;
  }
  const size_t lo = tx.find('<'), hi = tx.find('>');
  if (lo == std::string::npos || hi == std::string::npos || hi < lo) {
    std::printf("\nFAIL: no complete <...> payload within %llu cycles.\n",
                (unsigned long long)maxcyc);
    return 1;
  }
  const std::string got = tx.substr(lo + 1, hi - lo - 1);
  if (got != text) {
    std::printf("\nFAIL: payload mismatch\n  sent: ");
    for (char c : text) { if (c == '\n') std::printf("\\n"); else std::putchar(c); }
    std::printf("\n  got : ");
    for (char c : got)  { if (c == '\n') std::printf("\\n"); else std::putchar(c); }
    std::printf("\n");
    return 1;
  }

  std::printf("\nPASS: all %zu bytes made the round trip intact and in order.\n",
              text.size());
  return 0;
}
