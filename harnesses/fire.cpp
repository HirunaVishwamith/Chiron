// fire_run.cpp — bare-metal UART visualization runner (no emulator, no lock-step)
//
// Loads a bare-metal RV64 image into the RTL simulator and streams everything
// the program writes to the UART straight to this process's stdout, so ANSI/
// truecolor terminal programs (e.g. the Doom-fire demo in workloads/demos)
// render live in your terminal. All harness diagnostics go to stderr, so they
// never corrupt the rendered frames — you can redirect stdout to a file or pipe
// it without picking up status text.
//
// Usage:
//   ./fire_run.out --image <path> [--frames N] [--timeout <max_cycles>]
//                  [--done-pc <hex>]
//
//   --image    Path to RV64 binary image (required)
//   --frames   Stop after N full screen refreshes (ESC[H "home" sequences).
//              0 = run until --timeout. Default: 0.
//   --timeout  Maximum simulation cycles. Default: 2,000,000,000.
//   --done-pc  Optional completion PC for programs that actually exit.
//
// Exit codes: 0 = frames/done reached cleanly, 2 = hit cycle timeout.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <stdint.h>

#include "verilated.h"
#include "Vsystem.h"

using namespace std;

static inline void tick_nodump(Vsystem *tb) {
    tb->eval();
    tb->clock = 1;
    tb->eval();
    tb->clock = 0;
    tb->eval();
}

static bool load_image(Vsystem *tb, const string &image_path,
                       unsigned long long &tickcount) {
    ifstream input(image_path, ios::binary);
    if (!input.is_open()) {
        fprintf(stderr, "[fire_run] ERROR: cannot open image: %s\n",
                image_path.c_str());
        return false;
    }
    vector<unsigned char> buffer((istreambuf_iterator<char>(input)),
                                 istreambuf_iterator<char>());
    fprintf(stderr, "[fire_run] Loading image: %s (%zu bytes)\n",
            image_path.c_str(), buffer.size());

    tb->programmer_valid = 1;
    for (size_t i = 0; i + 7 < buffer.size(); i += 8) {
        tb->programmer_byte   = *reinterpret_cast<unsigned long *>(&buffer[i]);
        tb->programmer_offset = static_cast<unsigned long>(i);
        tick_nodump(tb);
        ++tickcount;
    }
    tb->finishedProgramming = 1;
    tb->programmer_valid    = 0;
    tick_nodump(tb); ++tickcount;
    tb->finishedProgramming = 0;
    tick_nodump(tb); ++tickcount;
    fprintf(stderr, "[fire_run] Image loaded.\n");
    return true;
}

static const char *find_arg(int argc, char **argv, const char *flag,
                             const char *def = nullptr) {
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return def;
}

int main(int argc, char *argv[]) {
    const char *image_path  = find_arg(argc, argv, "--image",
                                       "emulator/src/Image");
    const char *frames_str  = find_arg(argc, argv, "--frames", "0");
    const char *timeout_str = find_arg(argc, argv, "--timeout", "2000000000");
    const char *donepc_str  = find_arg(argc, argv, "--done-pc", nullptr);

    uint64_t max_frames = strtoull(frames_str, nullptr, 0);
    uint64_t max_cycles = strtoull(timeout_str, nullptr, 10);
    bool     have_done  = (donepc_str != nullptr);
    uint64_t done_pc    = have_done ? strtoull(donepc_str, nullptr, 0) : 0;

    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(false);
    Vsystem *tb = new Vsystem;
    unsigned long long tickcount = 0ULL;

    tb->reset = 1;
    for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }
    tb->reset = 0;
    for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }

    if (!load_image(tb, string(image_path), tickcount)) { delete tb; return 2; }

    fprintf(stderr, "[fire_run] Streaming UART (frames=%llu, max_cycles=%llu)..."
            "\n", (unsigned long long)max_frames, (unsigned long long)max_cycles);

    uint64_t sim_cycles = 0, frames = 0;
    int      exit_code  = 2;
    // 3-byte match window for the ESC[H "cursor home" frame-boundary sequence.
    unsigned char w0 = 0, w1 = 0;

    while (sim_cycles < max_cycles) {
        tick_nodump(tb);
        ++tickcount; ++sim_cycles;

        if (tb->putChar_valid) {
            unsigned char c = (unsigned char)tb->putChar_byte;
            putchar(c);
            // Detect "\033[H": flush the completed frame and count it.
            if (w0 == 0x1b && w1 == '[' && c == 'H') {
                fflush(stdout);
                if (max_frames && ++frames >= max_frames) {
                    fflush(stdout);
                    fprintf(stderr, "\n[fire_run] Reached %llu frames.\n",
                            (unsigned long long)frames);
                    exit_code = 0;
                    break;
                }
            }
            w0 = w1; w1 = c;
        }

        if (have_done && tb->robOut_commitFired &&
            (uint64_t)tb->robOut_pc == done_pc) {
            fflush(stdout);
            fprintf(stderr, "\n[fire_run] Program reached done-pc 0x%llx.\n",
                    (unsigned long long)done_pc);
            exit_code = 0;
            break;
        }
    }

    fflush(stdout);
    if (exit_code == 2)
        fprintf(stderr, "\n[fire_run] Stopped at cycle timeout (%llu).\n",
                (unsigned long long)sim_cycles);
    fprintf(stderr, "[fire_run] Sim cycles: %llu, frames: %llu\n",
            (unsigned long long)sim_cycles, (unsigned long long)frames);

    tb->final();
    delete tb;
    return exit_code;
}
