// profile_quad.cpp — quad-core profiling harness (no lock-step, no emulator).
//
// Loads a binary image compiled with NUM_CORES=4, runs all 4 RTL cores until
// core 0 signals completion (--done-pc or ecall exit), then reads all 4 cores'
// performance counters and writes a quad-core JSON report.
//
// Completion: core 0 hits a --done-pc (with optional --done-a0 gate) OR
//             core 0 sees gp==1 && a7==93 (ISA-test pass).
//
// Usage:
//   ./profile_quad.out --image <path> [--name <benchmark>]
//                      [--done-pc <hex> ...] [--done-a0 <val>]
//                      [--output <file.json>] [--timeout <max_cycles>]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdint.h>

#include "verilated.h"
#include "Vsystem.h"
#include "profiler_quad.h"

// Shared harness helpers: argv scanning, image loading, completion detection.
#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"
#include "sim/harness/common/completion.h"

using namespace std;
using namespace harness;

int main(int argc, char *argv[]) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        printf("Usage: %s --image <path> [--name <benchmark>]\n", argv[0]);
        printf("             [--done-pc <hex> ...] [--done-a0 <val>]\n");
        printf("             [--output <file.json>] [--timeout <max_cycles>]\n");
        return 0;
    }

    const char *image_path  = find_arg(argc, argv, "--image", "sim/data/Image");
    const char *output_path = find_arg(argc, argv, "--output", "");
    const char *bench_name  = find_arg(argc, argv, "--name", nullptr);
    const char *timeout_str = find_arg(argc, argv, "--timeout", "4000000000");
    const char *print_interval_str = find_arg(argc, argv, "--print-interval", "10000000");

    uint64_t max_cycles = strtoull(timeout_str, nullptr, 10);

    Completion completion = Completion::parse(argc, argv);

    string benchmark_name;
    if (bench_name) {
        benchmark_name = bench_name;
    } else {
        string p(image_path);
        size_t slash = p.find_last_of("/\\");
        benchmark_name = (slash == string::npos) ? p : p.substr(slash + 1);
        size_t dot = benchmark_name.find_last_of('.');
        if (dot != string::npos) benchmark_name = benchmark_name.substr(0, dot);
    }

    printf("[profile_quad] Benchmark : %s\n", benchmark_name.c_str());
    printf("[profile_quad] Max cycles: %llu\n", (unsigned long long)max_cycles);

    Verilated::commandArgs(argc, argv);
    Vsystem *tb = new Vsystem;
    Verilated::traceEverOn(false);

    unsigned long long tickcount = 0ULL;

    reset(tb, tickcount);
    if (!load_image(tb, string(image_path), tickcount, "[profile_quad]")) {
        delete tb;
        return 2;
    }

    ProfilerQuad profiler(tb);

    printf("[profile_quad] Starting simulation (max %llu cycles)...\n",
           (unsigned long long)max_cycles);

    uint64_t sim_cycles  = 0ULL;
    int      exit_code   = 2;
    uint64_t last_print  = 0ULL;
    uint64_t stall_cycles = 0ULL;
    std::string uart_log;
    const uint64_t PRINT_INTERVAL = strtoull(print_interval_str, nullptr, 10);
    const uint64_t MAX_STALL      = 500000ULL;

    while (sim_cycles < max_cycles) {
        tick_nodump(tb);
        ++tickcount;
        ++sim_cycles;

        if (sim_cycles - last_print >= PRINT_INTERVAL) {
            printf("[profile_quad] %llu cycles  C0=0x%llx C1=0x%llx C2=0x%llx C3=0x%llx\n",
                   (unsigned long long)sim_cycles,
                   (unsigned long long)tb->robOut0_pc,
                   (unsigned long long)tb->robOut1_pc,
                   (unsigned long long)tb->robOut2_pc,
                   (unsigned long long)tb->robOut3_pc);
            fflush(stdout);
            last_print = sim_cycles;
        }

        // UART output from all cores. Also keep a bounded log so we can
        // scrape "The code is ran with error code: N" — reaching the done-PC
        // is not enough; a wrong result array still prints a nonzero N.
        auto take_uart = [&](unsigned char byte) {
            putchar((int)byte);
            if (uart_log.size() < (1u << 20)) uart_log.push_back((char)byte);
        };
        if (tb->core0OutChar_valid) take_uart((unsigned char)tb->core0OutChar_byte);
        if (tb->core1OutChar_valid) take_uart((unsigned char)tb->core1OutChar_byte);
        if (tb->core2OutChar_valid) take_uart((unsigned char)tb->core2OutChar_byte);
        if (tb->core3OutChar_valid) take_uart((unsigned char)tb->core3OutChar_byte);
        if (tb->core0OutChar_valid || tb->core1OutChar_valid ||
            tb->core2OutChar_valid || tb->core3OutChar_valid) fflush(stdout);

        // Work-region windowing: must run before the early-continue below, so
        // cycles where core 0 does not commit still advance the other harts.
        profiler.sample(sim_cycles);

        // Completion is declared when core 0 (the coordinator hart) reaches
        // the terminal condition. At that point all cores have finished their
        // parallel work (they joined a barrier before core 0 exits).
        if (!tb->robOut0_commitFired) {
            if (++stall_cycles >= MAX_STALL) {
                printf("\n[profile_quad] DEADLOCK: no commit for %llu cycles at C0=0x%016llx\n",
                       (unsigned long long)stall_cycles,
                       (unsigned long long)tb->robOut0_pc);
                exit_code = 3;
                break;
            }
            continue;
        }
        stall_cycles = 0;

        uint64_t pc = tb->robOut0_pc;

        if (completion.active() && completion.hit(pc, tb->registersOut0_10)) {
            printf("\n[profile_quad] BENCHMARK COMPLETE (C0 PC=0x%016llx)\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }

        // ISA test exit (a7/x17 == 93): gp/x3 == 1 → pass, else fail.
        // Only meaningful when no done-pc spec is active: benchmarks use a7
        // as a scratch register, so a transient a7==93 mid-kernel would be
        // misread as an ISA exit (filter-q4 hits exactly that).
        IsaResult isa = completion.active()
                            ? IsaResult::Running
                            : isa_test_status(tb->registersOut0_3, tb->registersOut0_17);
        if (isa == IsaResult::Pass) {
            printf("\n[profile_quad] ISA TEST PASSED at PC=0x%016llx\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }
        if (isa == IsaResult::Fail) {
            printf("\n[profile_quad] ISA TEST FAILED (gp=0x%llx) at PC=0x%016llx\n",
                   (unsigned long long)tb->registersOut0_3,
                   (unsigned long long)pc);
            exit_code = 1;
            break;
        }
    }

    if (exit_code == 2)
        printf("\n[profile_quad] TIMEOUT after %llu cycles\n",
               (unsigned long long)sim_cycles);
    else if (exit_code == 3)
        printf("[profile_quad] DEADLOCK detected; partial profile written\n");

    int bench_error = -1;
    {
        const char *key = "error code: ";
        size_t pos = uart_log.rfind(key);
        if (pos != std::string::npos)
            bench_error = atoi(uart_log.c_str() + pos + strlen(key));
    }
    if (exit_code == 0 && bench_error > 0) {
        printf("[profile_quad] VERIFY FAIL: benchmark error code %d "
               "(done-PC hit but result array did not match)\n", bench_error);
        exit_code = 4;
    }

    printf("\n");
    profiler.print_summary();
    profiler.print_json(benchmark_name, string(output_path ? output_path : ""),
                        exit_code, bench_error);

    printf("[profile_quad] Total RTL ticks (including load): %llu\n",
           (unsigned long long)tickcount);
    printf("[profile_quad] Simulation cycles (post-reset):   %llu\n",
           (unsigned long long)sim_cycles);

    tb->final();
    delete tb;
    return exit_code;
}
