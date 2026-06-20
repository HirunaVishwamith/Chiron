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
        fprintf(stderr, "[profile_quad] ERROR: cannot open image: %s\n",
                image_path.c_str());
        return false;
    }
    vector<unsigned char> buffer(
        (istreambuf_iterator<char>(input)),
        istreambuf_iterator<char>()
    );
    printf("[profile_quad] Loading image: %s (%zu bytes)\n",
           image_path.c_str(), buffer.size());

    tb->programmer_valid = 1;
    for (size_t i = 0; i + 7 < buffer.size(); i += 8) {
        tb->programmer_byte   = *reinterpret_cast<unsigned long *>(&buffer[i]);
        tb->programmer_offset = static_cast<unsigned long>(i);
        tick_nodump(tb);
        ++tickcount;
        if ((i & 0xFFFFF) == 0) {
            printf("[profile_quad] Loaded: %3llu%%\r",
                   (unsigned long long)(i * 100 / buffer.size()));
            fflush(stdout);
        }
    }
    printf("[profile_quad] Image loaded (100%%)      \n");
    tb->finishedProgramming = 1;
    tb->programmer_valid    = 0;
    tick_nodump(tb); ++tickcount;
    tb->finishedProgramming = 0;
    tick_nodump(tb); ++tickcount;
    return true;
}

static const char *find_arg(int argc, char **argv, const char *flag,
                             const char *default_val = nullptr) {
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return default_val;
}

static bool has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], flag) == 0) return true;
    return false;
}

static vector<uint64_t> collect_hex_args(int argc, char **argv, const char *flag) {
    vector<uint64_t> out;
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], flag) == 0)
            out.push_back(strtoull(argv[i + 1], nullptr, 0));
    return out;
}

int main(int argc, char *argv[]) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        printf("Usage: %s --image <path> [--name <benchmark>]\n", argv[0]);
        printf("             [--done-pc <hex> ...] [--done-a0 <val>]\n");
        printf("             [--output <file.json>] [--timeout <max_cycles>]\n");
        return 0;
    }

    const char *image_path  = find_arg(argc, argv, "--image", "emulator/src/Image");
    const char *output_path = find_arg(argc, argv, "--output", "");
    const char *bench_name  = find_arg(argc, argv, "--name", nullptr);
    const char *timeout_str = find_arg(argc, argv, "--timeout", "4000000000");

    uint64_t max_cycles = strtoull(timeout_str, nullptr, 10);

    vector<uint64_t> done_pcs = collect_hex_args(argc, argv, "--done-pc");
    const char *done_a0_str   = find_arg(argc, argv, "--done-a0", nullptr);
    bool     have_a0_cond     = (done_a0_str != nullptr);
    uint64_t done_a0_val      = have_a0_cond ? strtoull(done_a0_str, nullptr, 0) : 0;

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

    tb->reset = 1;
    for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }
    tb->reset = 0;
    for (int i = 0; i < 20; ++i) { tick_nodump(tb); ++tickcount; }

    if (!load_image(tb, string(image_path), tickcount)) {
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
    const uint64_t PRINT_INTERVAL = 10000000ULL;
    const uint64_t MAX_STALL      = 500000ULL;

    while (sim_cycles < max_cycles) {
        tick_nodump(tb);
        ++tickcount;
        ++sim_cycles;

        if (sim_cycles - last_print >= PRINT_INTERVAL) {
            printf("[profile_quad] %llu M cycles  C0=0x%llx C1=0x%llx C2=0x%llx C3=0x%llx\r",
                   (unsigned long long)(sim_cycles / 1000000),
                   (unsigned long long)tb->robOut0_pc,
                   (unsigned long long)tb->robOut1_pc,
                   (unsigned long long)tb->robOut2_pc,
                   (unsigned long long)tb->robOut3_pc);
            fflush(stdout);
            last_print = sim_cycles;
        }

        // UART output from all cores
        if (tb->core0OutChar_valid) { putchar((int)tb->core0OutChar_byte); fflush(stdout); }
        if (tb->core1OutChar_valid) { putchar((int)tb->core1OutChar_byte); fflush(stdout); }
        if (tb->core2OutChar_valid) { putchar((int)tb->core2OutChar_byte); fflush(stdout); }
        if (tb->core3OutChar_valid) { putchar((int)tb->core3OutChar_byte); fflush(stdout); }

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

        if (!done_pcs.empty()) {
            bool pc_hit = false;
            for (uint64_t dpc : done_pcs) if (pc == dpc) { pc_hit = true; break; }
            if (pc_hit && (!have_a0_cond || tb->registersOut0_10 == done_a0_val)) {
                printf("\n[profile_quad] BENCHMARK COMPLETE (C0 PC=0x%016llx)\n",
                       (unsigned long long)pc);
                exit_code = 0;
                break;
            }
        }

        // ISA-test pass: gp==1 && a7==93
        if (tb->registersOut0_3 == 1 && tb->registersOut0_17 == 93) {
            printf("\n[profile_quad] ISA TEST PASSED at PC=0x%016llx\n",
                   (unsigned long long)pc);
            exit_code = 0;
            break;
        }

        if (tb->registersOut0_17 == 93) {
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

    printf("\n");
    profiler.print_summary();
    profiler.print_json(benchmark_name, string(output_path ? output_path : ""));

    printf("[profile_quad] Total RTL ticks (including load): %llu\n",
           (unsigned long long)tickcount);
    printf("[profile_quad] Simulation cycles (post-reset):   %llu\n",
           (unsigned long long)sim_cycles);

    tb->final();
    delete tb;
    return exit_code;
}
