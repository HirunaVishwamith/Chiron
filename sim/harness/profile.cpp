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

// Profiler (reads perfCountersOut0_* signals)
#include "profiler.h"

// Shared harness helpers: argv scanning, image loading, completion detection.
#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"
#include "sim/harness/common/completion.h"

using namespace std;
using namespace harness;

// ── ISA test pass PCs (secondary heuristic) ───────────────────────────────
// The riscv-tests harness reaches its `ecall` exit from one of these PC sites;
// the primary pass/fail signal is the a7==93 / gp check (see completion.h).
static const uint64_t PASS_PC_0 = 0x800009a0UL;
static const uint64_t PASS_PC_1 = 0x800009acUL;

// Benchmark-level completion: generic address used by benchmark exit stubs.
static const uint64_t BENCH_DONE_PC = 0x80000bc8UL;

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

    const char *image_path = find_arg(argc, argv, "--image", "sim/data/Image");
    const char *output_path = find_arg(argc, argv, "--output", "");
    const char *bench_name  = find_arg(argc, argv, "--name", nullptr);
    const char *timeout_str = find_arg(argc, argv, "--timeout", "2000000000");

    uint64_t max_cycles = static_cast<uint64_t>(strtoull(timeout_str, nullptr, 10));

    // Benchmark completion: each benchmark's exit/thread-join stub sits at a
    // different PC, so the caller passes the right one(s) via --done-pc (may be
    // repeated), optionally gated on a0 via --done-a0. See completion.h.
    Completion completion = Completion::parse(argc, argv);

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

    // ── Reset + image load ─────────────────────────────────────────────
    reset(tb, tickcount);
    if (!load_image(tb, string(image_path), tickcount, "[profile_run]")) {
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
    uint64_t stall_cycles = 0ULL;
    const uint64_t PRINT_INTERVAL  = 10000000ULL;
    const uint64_t MAX_STALL       = 500000ULL;  // 500K cycles with no commit = deadlock

    while (sim_cycles < max_cycles) {
        tick_nodump(tb);
        ++tickcount;
        ++sim_cycles;

        // Progress indicator every 10 M cycles
        if (sim_cycles - last_print >= PRINT_INTERVAL) {
            printf("[profile_run] %llu M cycles, PC=0x%016llx\r",
                   (unsigned long long)(sim_cycles / 1000000),
                   (unsigned long long)tb->robOut0_pc);
            fflush(stdout);
            last_print = sim_cycles;
        }

        // UART character output
        if (tb->core0OutChar_valid) {
            putchar(static_cast<int>(tb->core0OutChar_byte));
            fflush(stdout);
        }

        // Check for committed instruction
        if (!tb->robOut0_commitFired) {
            if (++stall_cycles >= MAX_STALL) {
                printf("\n[profile_run] DEADLOCK: no commit for %llu cycles at PC=0x%016llx\n",
                       (unsigned long long)stall_cycles,
                       (unsigned long long)tb->robOut0_pc);
                exit_code = 3;
                break;
            }
            continue;
        }
        stall_cycles = 0;

        uint64_t pc = tb->robOut0_pc;

        // Benchmark completion (highest priority): caller-supplied --done-pc,
        // optionally gated on a0. Checked before the ISA a7==93 heuristics so a
        // benchmark's exit stub is never misread as an ISA-test failure.
        if (completion.active() && completion.hit(pc, tb->registersOut0_10)) {
            printf("\n[profile_run] BENCHMARK COMPLETE (PC=0x%016llx)\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }

        // ISA test exit (a7/x17 == 93): gp/x3 == 1 → pass, else fail.
        IsaResult isa = isa_test_status(tb->registersOut0_3, tb->registersOut0_17);
        if (isa == IsaResult::Pass) {
            printf("\n[profile_run] ISA TEST PASSED (gp=1, a7=93) at PC=0x%016llx\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }
        if (isa == IsaResult::Fail) {
            printf("\n[profile_run] ISA TEST FAILED (gp=0x%llx, a7=93) at PC=0x%016llx\n",
                   (unsigned long long)tb->registersOut0_3,
                   (unsigned long long)pc);
            exit_code = 1;
            break;
        }

        // ISA pass PCs as secondary check
        if (pc == PASS_PC_0 || pc == PASS_PC_1) {
            if (tb->registersOut0_10 == 2 || tb->registersOut0_3 == 1) {
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
    } else if (exit_code == 3) {
        printf("[profile_run] DEADLOCK detected; partial profile written\n");
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
