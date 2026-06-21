// image.h — RTL bring-up shared by the raw-Vsystem harnesses (profile,
// profile_quad, fire). The lock-step harnesses use the richer simulator
// wrapper in sim/rtl/rtl_model.h instead.
//
// Three steps every harness repeats: advance one clock, hold reset, and stream
// a flat RV64 image into DRAM through the `programmer` port at offset 0.
#pragma once

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Vsystem.h"

namespace harness {

// One clock with no VCD dump: settle, rise, settle, fall, settle.
inline void tick_nodump(Vsystem *tb) {
  tb->eval();
  tb->clock = 1;
  tb->eval();
  tb->clock = 0;
  tb->eval();
}

// Hold reset asserted then released (20 cycles each) — the standard bring-up.
inline void reset(Vsystem *tb, unsigned long long &tickcount) {
  tb->reset = 1;
  for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }
  tb->reset = 0;
  for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }
}

// Stream a flat image into DRAM at offset 0 via the programmer port.
//   tag      log prefix, e.g. "[profile]"
//   log      where diagnostics go (fire streams UART to stdout, so it logs to
//            stderr to keep the rendered frames clean)
//   progress print a running percentage during the load
inline bool load_image(Vsystem *tb, const std::string &image_path,
                       unsigned long long &tickcount,
                       const char *tag = "[sim]", FILE *log = stdout,
                       bool progress = true) {
  std::ifstream input(image_path, std::ios::binary);
  if (!input.is_open()) {
    std::fprintf(stderr, "%s ERROR: cannot open image: %s\n", tag,
                 image_path.c_str());
    return false;
  }

  std::vector<unsigned char> buffer(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  std::fprintf(log, "%s Loading image: %s (%zu bytes)\n", tag,
               image_path.c_str(), buffer.size());

  tb->programmer_valid = 1;
  for (size_t i = 0; i + 7 < buffer.size(); i += 8) {
    tb->programmer_byte   = *reinterpret_cast<unsigned long *>(&buffer[i]);
    tb->programmer_offset = static_cast<unsigned long>(i);
    tick_nodump(tb);
    ++tickcount;
    if (progress && (i & 0xFFFFF) == 0) {
      std::fprintf(log, "%s Loaded: %3llu%%\r", tag,
                   (unsigned long long)(i * 100 / buffer.size()));
      std::fflush(log);
    }
  }
  std::fprintf(log, "%s Image loaded.%s\n", tag, progress ? "            " : "");

  tb->finishedProgramming = 1;
  tb->programmer_valid    = 0;
  tick_nodump(tb); ++tickcount;
  tb->finishedProgramming = 0;
  tick_nodump(tb); ++tickcount;
  return true;
}

}  // namespace harness
