// profile_run.cpp — standalone profiling harness (no lock-step, no emulator)
//
// Loads a binary image into the RTL simulator, runs it until a termination
// condition is detected or the cycle budget is exhausted, then reads all
// performance counters and writes a JSON report.
//
// Usage:
//   ./profile_run.out --image <path> [--name <benchmark>] \
//                     [--output <file.json>] [--timeout <max_cycles>]
//
// Exit codes:
//   0 = pass (ISA test passed or benchmark completed)
//   1 = fail (ISA test failed)
//   2 = timeout (max_cycles reached without termination)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdint.h>

// Pull in Verilator runtime and generated model
#include "verilated.h"
#include "Vsystem.h"

// Profiler (reads perfCountersOut_* signals)
#include "profiler.h"

using namespace std;

// ── ISA test termination PCs ──────────────────────────────────────────────
// The RISC-V ISA tests signal pass/fail via a syscall with a7=93 (exit).
// x3==1 && x17==93 → pass; x17==93 && x3!=1 → fail.
// These PC values are the ecall instruction sites in the riscv-tests harness.
static const uint64_t PASS_PC_0 = 0x800009a0UL;
static const uint64_t PASS_PC_1 = 0x800009acUL;

// Benchmark-level completion: generic address used by benchmark exit stubs.
// Set to 0 to disable; the harness uses x17==93 check universally instead.
static const uint64_t BENCH_DONE_PC = 0x80000bc8UL;

// ── Low-level tick (no VCD) ───────────────────────────────────────────────
static inline void tick_nodump(Vsystem *tb) {
    tb->eval();
    tb->clock = 1;
    tb->eval();
    tb->clock = 0;
    tb->eval();
}

// ── Memory image loader ───────────────────────────────────────────────────
static bool load_image(Vsystem *tb, const string &image_path,
                       unsigned long long &tickcount) {
    ifstream input(image_path, ios::binary);
    if (!input.is_open()) {
        fprintf(stderr, "[profile_run] ERROR: cannot open image: %s\n",
                image_path.c_str());
        return false;
    }

    vector<unsigned char> buffer(
        (istreambuf_iterator<char>(input)),
        istreambuf_iterator<char>()
    );

    printf("[profile_run] Loading image: %s (%zu bytes)\n",
           image_path.c_str(), buffer.size());

    tb->programmer_valid = 1;
    for (size_t i = 0; i + 7 < buffer.size(); i += 8) {
        tb->programmer_byte   = *reinterpret_cast<unsigned long *>(&buffer[i]);
        tb->programmer_offset = static_cast<unsigned long>(i);
        tick_nodump(tb);
        ++tickcount;
        if ((i & 0xFFFFF) == 0) {
            printf("[profile_run] Loaded: %3llu%%\r",
                   (unsigned long long)(i * 100 / buffer.size()));
            fflush(stdout);
        }
    }
    printf("[profile_run] Image loaded (100%%)      \n");

    tb->finishedProgramming = 1;
    tb->programmer_valid    = 0;
    tick_nodump(tb); ++tickcount;
    tb->finishedProgramming = 0;
    tick_nodump(tb); ++tickcount;

    return true;
}

// ── Argument parsing helpers ──────────────────────────────────────────────
static const char *find_arg(int argc, char **argv, const char *flag,
                             const char *default_val = nullptr) {
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return default_val;
}

static bool has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

// Collect every value following a repeated flag, e.g. all --done-pc <hex>.
static vector<uint64_t> collect_hex_args(int argc, char **argv, const char *flag) {
    vector<uint64_t> out;
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], flag) == 0)
            out.push_back(strtoull(argv[i + 1], nullptr, 0));
    }
    return out;
}

// ── Main ──────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {

    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        printf("Usage: %s --image <path> [--name <benchmark>]\n", argv[0]);
        printf("             [--output <file.json>] [--timeout <max_cycles>]\n");
        printf("\n");
        printf("  --image    Path to RV64 binary image (required)\n");
        printf("  --name     Benchmark name for JSON output (default: basename of image)\n");
        printf("  --output   Path to write JSON report (default: stdout only)\n");
        printf("  --timeout  Maximum simulation cycles (default: 2000000000)\n");
        return 0;
    }

    const char *image_path = find_arg(argc, argv, "--image", "emulator/src/Image");
    const char *output_path = find_arg(argc, argv, "--output", "");
    const char *bench_name  = find_arg(argc, argv, "--name", nullptr);
    const char *timeout_str = find_arg(argc, argv, "--timeout", "2000000000");

    uint64_t max_cycles = static_cast<uint64_t>(strtoull(timeout_str, nullptr, 10));

    // Benchmark completion: each benchmark's exit/thread-join stub sits at a
    // different PC, so the caller passes the right one(s) via --done-pc (may be
    // repeated). An optional --done-a0 adds a register condition (a0 == val),
    // matching the per-benchmark lock-step harnesses.
    vector<uint64_t> done_pcs = collect_hex_args(argc, argv, "--done-pc");
    const char *done_a0_str   = find_arg(argc, argv, "--done-a0", nullptr);
    bool     have_a0_cond     = (done_a0_str != nullptr);
    uint64_t done_a0_val      = have_a0_cond
                                ? strtoull(done_a0_str, nullptr, 0) : 0;

    // Derive benchmark name from image path if not provided
    string benchmark_name;
    if (bench_name) {
        benchmark_name = bench_name;
    } else {
        string p(image_path);
        size_t slash = p.find_last_of("/\\");
        benchmark_name = (slash == string::npos) ? p : p.substr(slash + 1);
        // strip extension
        size_t dot = benchmark_name.find_last_of('.');
        if (dot != string::npos) benchmark_name = benchmark_name.substr(0, dot);
    }

    printf("[profile_run] Benchmark : %s\n", benchmark_name.c_str());
    printf("[profile_run] Max cycles: %llu\n", (unsigned long long)max_cycles);

    // ── Initialise Verilator ───────────────────────────────────────────
    Verilated::commandArgs(argc, argv);
    Vsystem *tb = new Vsystem;

    // Disable VCD tracing for speed
    Verilated::traceEverOn(false);

    unsigned long long tickcount = 0ULL;

    // ── Reset sequence ─────────────────────────────────────────────────
    tb->reset = 1;
    for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }
    tb->reset = 0;
    for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }

    // ── Load image ─────────────────────────────────────────────────────
    if (!load_image(tb, string(image_path), tickcount)) {
        delete tb;
        return 2;
    }

    Profiler profiler(tb);

    // ── Simulation loop ─────────────────────────────────────────────────
    printf("[profile_run] Starting simulation (max %llu cycles)...\n",
           (unsigned long long)max_cycles);

    uint64_t sim_cycles   = 0ULL;
    int      exit_code    = 2;        // default: timeout
    uint64_t last_print   = 0ULL;
    const uint64_t PRINT_INTERVAL = 10000000ULL;

    while (sim_cycles < max_cycles) {
        tick_nodump(tb);
        ++tickcount;
        ++sim_cycles;

        // Progress indicator every 10 M cycles
        if (sim_cycles - last_print >= PRINT_INTERVAL) {
            printf("[profile_run] %llu M cycles, PC=0x%016llx\r",
                   (unsigned long long)(sim_cycles / 1000000),
                   (unsigned long long)tb->robOut_pc);
            fflush(stdout);
            last_print = sim_cycles;
        }

        // UART character output
        if (tb->putChar_valid) {
            putchar(static_cast<int>(tb->putChar_byte));
            fflush(stdout);
        }

        // Check for committed instruction
        if (!tb->robOut_commitFired) continue;

        uint64_t pc = tb->robOut_pc;

        // Benchmark completion (highest priority): caller-supplied --done-pc,
        // optionally gated on a0. Checked before the ISA a7==93 heuristics so a
        // benchmark's exit stub is never misread as an ISA-test failure.
        if (!done_pcs.empty()) {
            bool pc_hit = false;
            for (uint64_t dpc : done_pcs) if (pc == dpc) { pc_hit = true; break; }
            if (pc_hit && (!have_a0_cond || tb->registersOut_10 == done_a0_val)) {
                printf("\n[profile_run] BENCHMARK COMPLETE (PC=0x%016llx)\n",
                       (unsigned long long)pc);
                exit_code = 0;
                break;
            }
        }

        // ISA test: pass (gp/x3 == 1, a7/x17 == 93)
        if (tb->registersOut_3 == 1 && tb->registersOut_17 == 93) {
            printf("\n[profile_run] ISA TEST PASSED (gp=1, a7=93) at PC=0x%016llx\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }

        // ISA test: fail (a7 == 93 but gp != 1)
        if (tb->registersOut_17 == 93) {
            printf("\n[profile_run] ISA TEST FAILED (gp=0x%llx, a7=93) at PC=0x%016llx\n",
                   (unsigned long long)tb->registersOut_3,
                   (unsigned long long)pc);
            exit_code = 1;
            break;
        }

        // ISA pass PCs as secondary check
        if (pc == PASS_PC_0 || pc == PASS_PC_1) {
            if (tb->registersOut_10 == 2 || tb->registersOut_3 == 1) {
                printf("\n[profile_run] PASS (PC=0x%016llx)\n", (unsigned long long)pc);
                exit_code = 0;
                break;
            }
        }

        // Benchmark completion address
        if (pc == BENCH_DONE_PC) {
            printf("\n[profile_run] BENCHMARK COMPLETE (PC=0x%016llx)\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }
    }

    if (exit_code == 2) {
        printf("\n[profile_run] TIMEOUT after %llu cycles\n",
               (unsigned long long)sim_cycles);
    }

    // ── Collect and report performance data ────────────────────────────
    printf("\n");
    profiler.print_summary();
    profiler.print_json(benchmark_name, string(output_path ? output_path : ""));

    printf("[profile_run] Total RTL ticks (including load): %llu\n",
           (unsigned long long)tickcount);
    printf("[profile_run] Simulation cycles (post-reset):   %llu\n",
           (unsigned long long)sim_cycles);

    tb->final();
    delete tb;

    return exit_code;
}
