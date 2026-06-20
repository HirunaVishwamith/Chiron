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
    // Frontend bubble decomposition (Stage 0 instrumentation)
    uint64_t fe_fetch_not_ready;
    uint64_t fe_decode_not_ready;
    uint64_t fe_expected_block;
    uint64_t fe_resp_valid_idle;
    uint64_t fe_cache_not_prod;
    uint64_t fe_req_fire;
    uint64_t fe_req_refused;

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
            case  0: return tb->perfCountersOut0_0;
            case  1: return tb->perfCountersOut0_1;
            case  2: return tb->perfCountersOut0_2;
            case  3: return tb->perfCountersOut0_3;
            case  4: return tb->perfCountersOut0_4;
            case  5: return tb->perfCountersOut0_5;
            case  6: return tb->perfCountersOut0_6;
            case  7: return tb->perfCountersOut0_7;
            case  8: return tb->perfCountersOut0_8;
            case  9: return tb->perfCountersOut0_9;
            case 10: return tb->perfCountersOut0_10;
            case 11: return tb->perfCountersOut0_11;
            case 12: return tb->perfCountersOut0_12;
            case 13: return tb->perfCountersOut0_13;
            case 14: return tb->perfCountersOut0_14;
            case 15: return tb->perfCountersOut0_15;
            case 16: return tb->perfCountersOut0_16;
            case 17: return tb->perfCountersOut0_17;
            case 18: return tb->perfCountersOut0_18;
            case 19: return tb->perfCountersOut0_19;
            case 20: return tb->perfCountersOut0_20;
            case 21: return tb->perfCountersOut0_21;
            case 22: return tb->perfCountersOut0_22;
            case 23: return tb->perfCountersOut0_23;
            case 24: return tb->perfCountersOut0_24;
            case 25: return tb->perfCountersOut0_25;
            case 26: return tb->perfCountersOut0_26;
            case 27: return tb->perfCountersOut0_27;
            case 28: return tb->perfCountersOut0_28;
            case 29: return tb->perfCountersOut0_29;
            case 30: return tb->perfCountersOut0_30;
            case 31: return tb->perfCountersOut0_31;
            case 32: return tb->perfCountersOut0_32;
            case 33: return tb->perfCountersOut0_33;
            case 34: return tb->perfCountersOut0_34;
            case 35: return tb->perfCountersOut0_35;
            case 36: return tb->perfCountersOut0_36;
            case 37: return tb->perfCountersOut0_37;
            case 38: return tb->perfCountersOut0_38;
            case 39: return tb->perfCountersOut0_39;
            case 40: return tb->perfCountersOut0_40;
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
        m.fe_fetch_not_ready  = get_perf_counter(18);
        m.fe_decode_not_ready = get_perf_counter(19);
        m.fe_expected_block   = get_perf_counter(20);
        m.fe_resp_valid_idle  = get_perf_counter(21);
        m.fe_cache_not_prod   = get_perf_counter(22);
        m.fe_req_fire         = get_perf_counter(23);
        m.fe_req_refused      = get_perf_counter(24);

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
        ss << "    \"l2_to_mem_wr_beats\": "<< m.l2_to_mem_wr_beats<< ",\n";
        ss << "    \"fe_fetch_not_ready\": " << m.fe_fetch_not_ready << ",\n";
        ss << "    \"fe_decode_not_ready\": "<< m.fe_decode_not_ready<< ",\n";
        ss << "    \"fe_expected_block\": "  << m.fe_expected_block  << ",\n";
        ss << "    \"fe_resp_valid_idle\": " << m.fe_resp_valid_idle << ",\n";
        ss << "    \"fe_cache_not_prod\": "  << m.fe_cache_not_prod  << ",\n";
        ss << "    \"fe_req_fire\": "        << m.fe_req_fire        << ",\n";
        ss << "    \"fe_req_refused\": "     << m.fe_req_refused     << "\n";
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
        printf("\n--- Frontend Bubble Decomposition (%% of cycles) ---\n");
        double sc = static_cast<double>(safe_max1(m.cycles));
        printf("  Fetch not ready:   %19.2f%%  (%llu)\n",
               100.0 * m.fe_fetch_not_ready / sc,  (unsigned long long)m.fe_fetch_not_ready);
        printf("  Decode backpress:  %19.2f%%  (%llu)\n",
               100.0 * m.fe_decode_not_ready / sc, (unsigned long long)m.fe_decode_not_ready);
        printf("  Expected mismatch: %19.2f%%  (%llu)\n",
               100.0 * m.fe_expected_block / sc,   (unsigned long long)m.fe_expected_block);
        printf("  I$ resp idle:      %19.2f%%  (%llu)\n",
               100.0 * m.fe_resp_valid_idle / sc,  (unsigned long long)m.fe_resp_valid_idle);
        printf("  -- of which / I$ boundary --\n");
        printf("  I$ not producing:  %19.2f%%  (%llu)\n",
               100.0 * m.fe_cache_not_prod / sc,   (unsigned long long)m.fe_cache_not_prod);
        printf("  Req fired:         %19.2f%%  (%llu)\n",
               100.0 * m.fe_req_fire / sc,         (unsigned long long)m.fe_req_fire);
        printf("  Req refused by I$: %19.2f%%  (%llu)\n",
               100.0 * m.fe_req_refused / sc,      (unsigned long long)m.fe_req_refused);
        printf("  -- streamer internals --\n");
        printf("  curValid:          %19.2f%%  (%llu)\n",
               100.0 * get_perf_counter(25) / sc, (unsigned long long)get_perf_counter(25));
        printf("  validMiss(bug):    %19.2f%%  (%llu)\n",
               100.0 * get_perf_counter(26) / sc, (unsigned long long)get_perf_counter(26));
        {
          unsigned long long fillActive = get_perf_counter(27);
          unsigned long long fillStarts = get_perf_counter(28);
          printf("  fill-active cyc:   %19.2f%%  (%llu)\n", 100.0 * fillActive / sc, fillActive);
          printf("  fill starts:       %20llu   avg latency: %.1f cyc/fill\n",
                 fillStarts, fillStarts ? (double)fillActive / fillStarts : 0.0);
        }
        printf("  -- ROB head stall split (B1) --\n");
        {
          unsigned long long headNotReady = get_perf_counter(29);
          unsigned long long readyBlocked = get_perf_counter(30);
          printf("  head not ready:    %19.2f%%  (%llu)  [latency-bound]\n",
                 100.0 * headNotReady / sc, headNotReady);
          printf("  ready, no retire:  %19.2f%%  (%llu)  [commit-width-bound]\n",
                 100.0 * readyBlocked / sc, readyBlocked);
          unsigned long long hnrL = get_perf_counter(31), hnrB = get_perf_counter(32),
                             hnrM = get_perf_counter(33), hnrA = get_perf_counter(34),
                             hnrO = get_perf_counter(35);
          printf("  -- head-not-ready by class --\n");
          printf("    load:            %19.2f%%  (%llu)\n", 100.0 * hnrL / sc, hnrL);
          printf("    branch/jump:     %19.2f%%  (%llu)\n", 100.0 * hnrB / sc, hnrB);
          printf("    M-ext (mul/div): %19.2f%%  (%llu)\n", 100.0 * hnrM / sc, hnrM);
          printf("    AMO:             %19.2f%%  (%llu)\n", 100.0 * hnrA / sc, hnrA);
          printf("    other (ALU/sys): %19.2f%%  (%llu)\n", 100.0 * hnrO / sc, hnrO);
          unsigned long long rnrS = get_perf_counter(36), rnrW = get_perf_counter(37),
                             rnrL = get_perf_counter(38);
          printf("  -- ready-no-retire by gate --\n");
          printf("    store wr-commit: %19.2f%%  (%llu)\n", 100.0 * rnrS / sc, rnrS);
          printf("    writeback port:  %19.2f%%  (%llu)\n", 100.0 * rnrW / sc, rnrW);
          printf("    load commit:     %19.2f%%  (%llu)\n", 100.0 * rnrL / sc, rnrL);
          unsigned long long iss2 = get_perf_counter(39), com2 = get_perf_counter(40);
          printf("  -- 2-wide opportunity (W0) --\n");
          printf("    issue >=2 ready: %19.2f%%  (%llu)  [2-wide issue ceiling]\n",
                 100.0 * iss2 / sc, iss2);
          printf("    commit 2 ready:  %19.2f%%  (%llu)  [2-wide commit ceiling]\n",
                 100.0 * com2 / sc, com2);
        }
        printf("=========================================\n\n");
    }
};
