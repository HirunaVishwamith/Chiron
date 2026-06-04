#pragma once

#include <cstdint>
#include <string>
#include <cstdio>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "Vsystem.h"

// Counter index mapping (matches perfCountersOut Vec in system.scala)
// Core counters (0-9):
//   0  cycles
//   1  instRetired
//   2  branchTotal
//   3  branchesPassed
//   4  schedulerStalls
//   5  robStalls
//   6  decodeReady
//   7  decodeFired
//   8  icacheStalls
//   9  dcacheReqs
// System counters (10-17):
//   10 dCacheMiss    (D-Cache -> L2 read requests)
//   11 dCacheRdBeats (D-Cache read data beats received)
//   12 dCacheWrBeats (D-Cache write data beats sent)
//   13 iCacheMiss    (I-Cache -> L2 read requests)
//   14 iCacheRdBeats (I-Cache read data beats received)
//   15 l2ToMemRdReqs (L2 -> DRAM read requests)
//   16 l2ToMemRdBeats(L2 -> DRAM read data beats)
//   17 l2ToMemWrBeats(L2 -> DRAM write data beats)

struct PerfMetrics {
    // Raw counters
    uint64_t cycles;
    uint64_t inst_retired;
    uint64_t branch_total;
    uint64_t branches_passed;
    uint64_t scheduler_stalls;
    uint64_t rob_stalls;
    uint64_t decode_ready;
    uint64_t decode_fired;
    uint64_t icache_stalls;
    uint64_t dcache_reqs;
    uint64_t dcache_miss;
    uint64_t dcache_rd_beats;
    uint64_t dcache_wr_beats;
    uint64_t icache_miss;
    uint64_t icache_rd_beats;
    uint64_t l2_to_mem_rd_reqs;
    uint64_t l2_to_mem_rd_beats;
    uint64_t l2_to_mem_wr_beats;

    // Derived metrics
    double ipc;
    double ip_per_sec;
    double branch_accuracy_pct;
    double icache_miss_rate_pct;
    double dcache_miss_rate_pct;
    double l1d_read_bw_bytes_per_cycle;
    double l1d_write_bw_bytes_per_cycle;
    double l1i_read_bw_bytes_per_cycle;
    double dram_read_bw_MB_per_sec;
    double dram_write_bw_MB_per_sec;
    double scheduler_stall_pct;
    double rob_stall_pct;
    double decode_efficiency_pct;
    double dcache_mem_reqs_per_million_cycles;
};

class Profiler {
private:
    Vsystem *tb;

    static inline uint64_t safe_max1(uint64_t v) {
        return v > 0 ? v : 1ULL;
    }

public:
    explicit Profiler(Vsystem *tb_ptr) : tb(tb_ptr) {}

    uint64_t get_perf_counter(int id) const {
        switch (id) {
            case  0: return tb->perfCountersOut_0;
            case  1: return tb->perfCountersOut_1;
            case  2: return tb->perfCountersOut_2;
            case  3: return tb->perfCountersOut_3;
            case  4: return tb->perfCountersOut_4;
            case  5: return tb->perfCountersOut_5;
            case  6: return tb->perfCountersOut_6;
            case  7: return tb->perfCountersOut_7;
            case  8: return tb->perfCountersOut_8;
            case  9: return tb->perfCountersOut_9;
            case 10: return tb->perfCountersOut_10;
            case 11: return tb->perfCountersOut_11;
            case 12: return tb->perfCountersOut_12;
            case 13: return tb->perfCountersOut_13;
            case 14: return tb->perfCountersOut_14;
            case 15: return tb->perfCountersOut_15;
            case 16: return tb->perfCountersOut_16;
            case 17: return tb->perfCountersOut_17;
            default: return 0ULL;
        }
    }

    PerfMetrics compute_metrics() const {
        PerfMetrics m;

        // Read all raw counters
        m.cycles            = get_perf_counter(0);
        m.inst_retired      = get_perf_counter(1);
        m.branch_total      = get_perf_counter(2);
        m.branches_passed   = get_perf_counter(3);
        m.scheduler_stalls  = get_perf_counter(4);
        m.rob_stalls        = get_perf_counter(5);
        m.decode_ready      = get_perf_counter(6);
        m.decode_fired      = get_perf_counter(7);
        m.icache_stalls     = get_perf_counter(8);
        m.dcache_reqs       = get_perf_counter(9);
        m.dcache_miss       = get_perf_counter(10);
        m.dcache_rd_beats   = get_perf_counter(11);
        m.dcache_wr_beats   = get_perf_counter(12);
        m.icache_miss       = get_perf_counter(13);
        m.icache_rd_beats   = get_perf_counter(14);
        m.l2_to_mem_rd_reqs = get_perf_counter(15);
        m.l2_to_mem_rd_beats= get_perf_counter(16);
        m.l2_to_mem_wr_beats= get_perf_counter(17);

        const double clock_hz = 75000000.0;
        const double bytes_per_beat = 8.0;

        uint64_t safe_cycles    = safe_max1(m.cycles);
        uint64_t safe_decode_ready = safe_max1(m.decode_ready);
        uint64_t safe_branch_total = safe_max1(m.branch_total);
        uint64_t safe_dcache_reqs  = safe_max1(m.dcache_reqs);

        m.ipc = static_cast<double>(m.inst_retired) / static_cast<double>(safe_cycles);
        m.ip_per_sec = m.ipc * clock_hz;

        m.branch_accuracy_pct =
            100.0 * static_cast<double>(m.branches_passed) / static_cast<double>(safe_branch_total);

        // I-Cache miss rate: miss events per decode-ready cycle
        m.icache_miss_rate_pct =
            100.0 * static_cast<double>(m.icache_miss) / static_cast<double>(safe_decode_ready);

        // D-Cache miss rate: L1D read misses per D-cache request cycle
        m.dcache_miss_rate_pct =
            100.0 * static_cast<double>(m.dcache_miss) / static_cast<double>(safe_dcache_reqs);

        m.l1d_read_bw_bytes_per_cycle =
            static_cast<double>(m.dcache_rd_beats) * bytes_per_beat / static_cast<double>(safe_cycles);

        m.l1d_write_bw_bytes_per_cycle =
            static_cast<double>(m.dcache_wr_beats) * bytes_per_beat / static_cast<double>(safe_cycles);

        m.l1i_read_bw_bytes_per_cycle =
            static_cast<double>(m.icache_rd_beats) * bytes_per_beat / static_cast<double>(safe_cycles);

        m.dram_read_bw_MB_per_sec =
            static_cast<double>(m.l2_to_mem_rd_beats) * bytes_per_beat
            / static_cast<double>(safe_cycles) * clock_hz / 1e6;

        m.dram_write_bw_MB_per_sec =
            static_cast<double>(m.l2_to_mem_wr_beats) * bytes_per_beat
            / static_cast<double>(safe_cycles) * clock_hz / 1e6;

        m.scheduler_stall_pct =
            100.0 * static_cast<double>(m.scheduler_stalls) / static_cast<double>(safe_decode_ready);

        m.rob_stall_pct =
            100.0 * static_cast<double>(m.rob_stalls) / static_cast<double>(safe_decode_ready);

        m.decode_efficiency_pct =
            100.0 * static_cast<double>(m.decode_fired) / static_cast<double>(safe_decode_ready);

        m.dcache_mem_reqs_per_million_cycles =
            static_cast<double>(m.dcache_reqs) / static_cast<double>(safe_cycles) * 1e6;

        return m;
    }

    void print_json(const std::string &benchmark_name, const std::string &output_path = "") const {
        PerfMetrics m = compute_metrics();

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\n";
        ss << "  \"benchmark\": \"" << benchmark_name << "\",\n";
        ss << "  \"raw\": {\n";
        ss << "    \"cycles\": "            << m.cycles            << ",\n";
        ss << "    \"inst_retired\": "      << m.inst_retired      << ",\n";
        ss << "    \"branch_total\": "      << m.branch_total      << ",\n";
        ss << "    \"branches_passed\": "   << m.branches_passed   << ",\n";
        ss << "    \"scheduler_stalls\": "  << m.scheduler_stalls  << ",\n";
        ss << "    \"rob_stalls\": "        << m.rob_stalls        << ",\n";
        ss << "    \"decode_ready\": "      << m.decode_ready      << ",\n";
        ss << "    \"decode_fired\": "      << m.decode_fired      << ",\n";
        ss << "    \"icache_stalls\": "     << m.icache_stalls     << ",\n";
        ss << "    \"dcache_reqs\": "       << m.dcache_reqs       << ",\n";
        ss << "    \"dcache_miss\": "       << m.dcache_miss       << ",\n";
        ss << "    \"dcache_rd_beats\": "   << m.dcache_rd_beats   << ",\n";
        ss << "    \"dcache_wr_beats\": "   << m.dcache_wr_beats   << ",\n";
        ss << "    \"icache_miss\": "       << m.icache_miss       << ",\n";
        ss << "    \"icache_rd_beats\": "   << m.icache_rd_beats   << ",\n";
        ss << "    \"l2_to_mem_rd_reqs\": " << m.l2_to_mem_rd_reqs << ",\n";
        ss << "    \"l2_to_mem_rd_beats\": "<< m.l2_to_mem_rd_beats<< ",\n";
        ss << "    \"l2_to_mem_wr_beats\": "<< m.l2_to_mem_wr_beats<< "\n";
        ss << "  },\n";
        ss << "  \"derived\": {\n";
        ss << "    \"ipc\": "                             << m.ipc                           << ",\n";
        ss << "    \"ip_per_sec\": "                      << std::setprecision(0) << m.ip_per_sec << std::setprecision(4) << ",\n";
        ss << "    \"branch_accuracy_pct\": "             << m.branch_accuracy_pct           << ",\n";
        ss << "    \"icache_miss_rate_pct\": "            << m.icache_miss_rate_pct          << ",\n";
        ss << "    \"dcache_miss_rate_pct\": "            << m.dcache_miss_rate_pct          << ",\n";
        ss << "    \"l1d_read_bw_bytes_per_cycle\": "     << m.l1d_read_bw_bytes_per_cycle   << ",\n";
        ss << "    \"l1d_write_bw_bytes_per_cycle\": "    << m.l1d_write_bw_bytes_per_cycle  << ",\n";
        ss << "    \"l1i_read_bw_bytes_per_cycle\": "     << m.l1i_read_bw_bytes_per_cycle   << ",\n";
        ss << "    \"dram_read_bw_MB_per_sec\": "         << m.dram_read_bw_MB_per_sec       << ",\n";
        ss << "    \"dram_write_bw_MB_per_sec\": "        << m.dram_write_bw_MB_per_sec      << ",\n";
        ss << "    \"scheduler_stall_pct\": "             << m.scheduler_stall_pct           << ",\n";
        ss << "    \"rob_stall_pct\": "                   << m.rob_stall_pct                 << ",\n";
        ss << "    \"decode_efficiency_pct\": "           << m.decode_efficiency_pct         << ",\n";
        ss << "    \"dcache_mem_reqs_per_million_cycles\": " << m.dcache_mem_reqs_per_million_cycles << "\n";
        ss << "  }\n";
        ss << "}\n";

        std::string json_str = ss.str();

        // Print to stdout
        printf("%s", json_str.c_str());

        // Write to file if path provided
        if (!output_path.empty()) {
            std::ofstream out(output_path);
            if (out.is_open()) {
                out << json_str;
                out.close();
                printf("[profiler] JSON written to: %s\n", output_path.c_str());
            } else {
                fprintf(stderr, "[profiler] ERROR: could not open output file: %s\n", output_path.c_str());
            }
        }
    }

    void print_summary() const {
        PerfMetrics m = compute_metrics();

        printf("\n========== Performance Summary ==========\n");
        printf("  Cycles:            %20llu\n", (unsigned long long)m.cycles);
        printf("  Instructions:      %20llu\n", (unsigned long long)m.inst_retired);
        printf("  IPC:               %20.4f\n", m.ipc);
        printf("  IP/sec (75 MHz):   %20.0f\n", m.ip_per_sec);
        printf("\n--- Branch Prediction ---\n");
        printf("  Branch total:      %20llu\n", (unsigned long long)m.branch_total);
        printf("  Branches passed:   %20llu\n", (unsigned long long)m.branches_passed);
        printf("  Accuracy:          %19.2f%%\n", m.branch_accuracy_pct);
        printf("\n--- Cache Performance ---\n");
        printf("  I-Cache misses:    %20llu\n", (unsigned long long)m.icache_miss);
        printf("  I-Cache miss rate: %19.2f%%\n", m.icache_miss_rate_pct);
        printf("  D-Cache req cyc:   %20llu\n", (unsigned long long)m.dcache_reqs);
        printf("  D-Cache misses:    %20llu\n", (unsigned long long)m.dcache_miss);
        printf("  D-Cache miss rate: %19.2f%%\n", m.dcache_miss_rate_pct);
        printf("\n--- Memory Bandwidth ---\n");
        printf("  L1D read BW:       %15.4f B/cyc\n", m.l1d_read_bw_bytes_per_cycle);
        printf("  L1D write BW:      %15.4f B/cyc\n", m.l1d_write_bw_bytes_per_cycle);
        printf("  L1I read BW:       %15.4f B/cyc\n", m.l1i_read_bw_bytes_per_cycle);
        printf("  DRAM read BW:      %15.2f MB/s\n",  m.dram_read_bw_MB_per_sec);
        printf("  DRAM write BW:     %15.2f MB/s\n",  m.dram_write_bw_MB_per_sec);
        printf("\n--- Pipeline Utilization ---\n");
        printf("  Scheduler stalls:  %19.2f%%\n", m.scheduler_stall_pct);
        printf("  ROB stalls:        %19.2f%%\n", m.rob_stall_pct);
        printf("  Decode efficiency: %19.2f%%\n", m.decode_efficiency_pct);
        printf("  D$ req/Mcyc:       %20.2f\n",   m.dcache_mem_reqs_per_million_cycles);
        printf("=========================================\n\n");
    }
};
