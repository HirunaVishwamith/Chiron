// image_load_probe.cpp — does the image actually arrive in DRAM intact?
//
// Every bare-metal harness (profile, profile_quad, fire) streams its image in
// through the RTL's programmer port, 8 bytes per clock. This probe does that
// same load and then compares all of DRAM against the file on disk, byte for
// byte, so a dropped or mis-addressed write shows up as an address rather than
// as a wrong answer 9 million cycles later.
//
//   usage: image_load_probe.out <image.bin> [--max-report N]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <stdint.h>

#include "verilated.h"
#include "Vsystem.h"
#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"

using namespace harness;

int main(int argc, char **argv) {
  const char *image = (argc > 1 && argv[1][0] != '-') ? argv[1]
                    : harness::find_arg(argc, argv, "--image", "bins/mt-vvadd-s1.bin");
  const long max_report =
      std::strtol(harness::find_arg(argc, argv, "--max-report", "20"), nullptr, 0);

  std::ifstream in(image, std::ios::binary);
  if (!in) { std::fprintf(stderr, "cannot open %s\n", image); return 2; }
  std::vector<unsigned char> want((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

  Verilated::commandArgs(argc, argv);
  Vsystem *tb = new Vsystem;
  Verilated::traceEverOn(false);
  unsigned long long tick = 0;
  reset(tb, tick);
  if (!load_image(tb, std::string(image), tick, "[imgprobe]", stdout, false)) return 2;

  const unsigned char *dram =
      reinterpret_cast<const unsigned char *>(tb->system__DOT__memory__DOT__memory);

  std::printf("[imgprobe] image %s: %zu bytes (%zu %% 8 = %zu)\n",
              image, want.size(), want.size(), want.size() % 8);

  long bad = 0;
  size_t first = 0, last = 0;
  for (size_t i = 0; i < want.size(); ++i) {
    if (dram[i] == want[i]) continue;
    if (!bad) first = i;
    last = i;
    if (bad < max_report)
      std::printf("  offset %8zu (addr 0x%08zx): dram=0x%02x file=0x%02x\n",
                  i, 0x80000000UL + i, dram[i], want[i]);
    ++bad;
  }
  if (bad)
    std::printf("[imgprobe] MISMATCH: %ld bytes differ (first %zu, last %zu)\n",
                bad, first, last);
  else
    std::printf("[imgprobe] OK: DRAM matches the file exactly\n");
  delete tb;
  return bad ? 1 : 0;
}
