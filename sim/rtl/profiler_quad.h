#pragma once

// profiler_quad.h — reads perfCountersOut0..3 from the quad-core RTL.
//
// Each of the 4 cores exposes Vec(41, UInt(64.W)) with the same slot layout as
// the single-core profiler.h.  Slots [21-28] are always 0 (no lineStreamer in
// the quad-core fetch).
//
// Aggregate IPC = sum(inst_retired) / max(cycles) — total instructions per
// wall-clock cycle, the number that matters for throughput comparison.

#include <cstdint>
#include <string>
#include <cstdio>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "Vsystem.h"
#include "profiler.h"   // reuse PerfMetrics struct

class ProfilerQuad {
private:
    Vsystem *tb;

    static inline uint64_t safe_max1(uint64_t v) { return v > 0 ? v : 1ULL; }

    // ── Work-region windowing ────────────────────────────────────────────────
    // Harts 1-3 finish their slice and then spin in barrier()'s busy-wait
    // (workloads/benchmarks/common/util.h) for most of the run, so whole-run
    // per-hart counters largely describe a 1-2 instruction branch loop rather
    // than the benchmark. Classify each block of commits by the PC span it
    // covers -- a tight spin loop spans a handful of bytes, real code spans far
    // more -- and snapshot every counter at the end of the last block that
    // looked like real work. That snapshot is the work-region measurement.
    //
    // Deliberately conservative: a block straddling the work/spin boundary has
    // a wide span and counts as work, so the window errs towards including too
    // much rather than truncating real work.
    static const int      BLOCK_COMMITS   = 64;
    static const uint64_t WORK_SPAN_BYTES = 16;  // > 4 instructions == real code

    struct WorkWin {
        uint64_t blk_lo = ~0ULL, blk_hi = 0;
        int      blk_n = 0;
        uint64_t commits = 0, spin_commits = 0;
        uint64_t spin_lo = ~0ULL, spin_hi = 0;
        uint64_t work_cycle = 0;
        uint64_t snap[41] = {0};
        bool     have_snap = false;
    };
    WorkWin win[4];

public:
    explicit ProfilerQuad(Vsystem *tb_ptr) : tb(tb_ptr) {}

    // Call once per cycle, before any early-continue in the harness loop.
    void sample(uint64_t cycle) {
        const uint8_t fired[4] = { tb->robOut0_commitFired, tb->robOut1_commitFired,
                                   tb->robOut2_commitFired, tb->robOut3_commitFired };
        const uint64_t pcs[4]  = { tb->robOut0_pc, tb->robOut1_pc,
                                   tb->robOut2_pc, tb->robOut3_pc };
        for (int i = 0; i < 4; ++i) {
            if (!fired[i]) continue;
            WorkWin &w = win[i];
            ++w.commits;
            if (pcs[i] < w.blk_lo) w.blk_lo = pcs[i];
            if (pcs[i] > w.blk_hi) w.blk_hi = pcs[i];
            if (++w.blk_n < BLOCK_COMMITS) continue;
            if (w.blk_hi - w.blk_lo > WORK_SPAN_BYTES) {
                w.work_cycle = cycle;
                read_counters(i, w.snap);
                w.have_snap = true;
            } else {
                w.spin_commits += BLOCK_COMMITS;
                if (w.blk_lo < w.spin_lo) w.spin_lo = w.blk_lo;
                if (w.blk_hi > w.spin_hi) w.spin_hi = w.blk_hi;
            }
            w.blk_lo = ~0ULL; w.blk_hi = 0; w.blk_n = 0;
        }
    }

    double spin_commit_pct(int c) const {
        return 100.0 * (double)win[c].spin_commits / (double)safe_max1(win[c].commits);
    }

    uint64_t get_core_counter(int core, int id) const {
        switch (core) {
        case 0: switch (id) {
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
            default: return 0ULL; }
        case 1: switch (id) {
            case  0: return tb->perfCountersOut1_0;
            case  1: return tb->perfCountersOut1_1;
            case  2: return tb->perfCountersOut1_2;
            case  3: return tb->perfCountersOut1_3;
            case  4: return tb->perfCountersOut1_4;
            case  5: return tb->perfCountersOut1_5;
            case  6: return tb->perfCountersOut1_6;
            case  7: return tb->perfCountersOut1_7;
            case  8: return tb->perfCountersOut1_8;
            case  9: return tb->perfCountersOut1_9;
            case 10: return tb->perfCountersOut1_10;
            case 11: return tb->perfCountersOut1_11;
            case 12: return tb->perfCountersOut1_12;
            case 13: return tb->perfCountersOut1_13;
            case 14: return tb->perfCountersOut1_14;
            case 15: return tb->perfCountersOut1_15;
            case 16: return tb->perfCountersOut1_16;
            case 17: return tb->perfCountersOut1_17;
            case 18: return tb->perfCountersOut1_18;
            case 19: return tb->perfCountersOut1_19;
            case 20: return tb->perfCountersOut1_20;
            case 21: return tb->perfCountersOut1_21;
            case 22: return tb->perfCountersOut1_22;
            case 23: return tb->perfCountersOut1_23;
            case 24: return tb->perfCountersOut1_24;
            case 25: return tb->perfCountersOut1_25;
            case 26: return tb->perfCountersOut1_26;
            case 29: return tb->perfCountersOut1_29;
            case 30: return tb->perfCountersOut1_30;
            case 31: return tb->perfCountersOut1_31;
            case 32: return tb->perfCountersOut1_32;
            case 33: return tb->perfCountersOut1_33;
            case 34: return tb->perfCountersOut1_34;
            case 35: return tb->perfCountersOut1_35;
            case 36: return tb->perfCountersOut1_36;
            case 37: return tb->perfCountersOut1_37;
            case 38: return tb->perfCountersOut1_38;
            case 39: return tb->perfCountersOut1_39;
            case 40: return tb->perfCountersOut1_40;
            default: return 0ULL; }
        case 2: switch (id) {
            case  0: return tb->perfCountersOut2_0;
            case  1: return tb->perfCountersOut2_1;
            case  2: return tb->perfCountersOut2_2;
            case  3: return tb->perfCountersOut2_3;
            case  4: return tb->perfCountersOut2_4;
            case  5: return tb->perfCountersOut2_5;
            case  6: return tb->perfCountersOut2_6;
            case  7: return tb->perfCountersOut2_7;
            case  8: return tb->perfCountersOut2_8;
            case  9: return tb->perfCountersOut2_9;
            case 10: return tb->perfCountersOut2_10;
            case 11: return tb->perfCountersOut2_11;
            case 12: return tb->perfCountersOut2_12;
            case 13: return tb->perfCountersOut2_13;
            case 14: return tb->perfCountersOut2_14;
            case 15: return tb->perfCountersOut2_15;
            case 16: return tb->perfCountersOut2_16;
            case 17: return tb->perfCountersOut2_17;
            case 18: return tb->perfCountersOut2_18;
            case 19: return tb->perfCountersOut2_19;
            case 20: return tb->perfCountersOut2_20;
            case 21: return tb->perfCountersOut2_21;
            case 22: return tb->perfCountersOut2_22;
            case 23: return tb->perfCountersOut2_23;
            case 24: return tb->perfCountersOut2_24;
            case 25: return tb->perfCountersOut2_25;
            case 26: return tb->perfCountersOut2_26;
            case 29: return tb->perfCountersOut2_29;
            case 30: return tb->perfCountersOut2_30;
            case 31: return tb->perfCountersOut2_31;
            case 32: return tb->perfCountersOut2_32;
            case 33: return tb->perfCountersOut2_33;
            case 34: return tb->perfCountersOut2_34;
            case 35: return tb->perfCountersOut2_35;
            case 36: return tb->perfCountersOut2_36;
            case 37: return tb->perfCountersOut2_37;
            case 38: return tb->perfCountersOut2_38;
            case 39: return tb->perfCountersOut2_39;
            case 40: return tb->perfCountersOut2_40;
            default: return 0ULL; }
        case 3: switch (id) {
            case  0: return tb->perfCountersOut3_0;
            case  1: return tb->perfCountersOut3_1;
            case  2: return tb->perfCountersOut3_2;
            case  3: return tb->perfCountersOut3_3;
            case  4: return tb->perfCountersOut3_4;
            case  5: return tb->perfCountersOut3_5;
            case  6: return tb->perfCountersOut3_6;
            case  7: return tb->perfCountersOut3_7;
            case  8: return tb->perfCountersOut3_8;
            case  9: return tb->perfCountersOut3_9;
            case 10: return tb->perfCountersOut3_10;
            case 11: return tb->perfCountersOut3_11;
            case 12: return tb->perfCountersOut3_12;
            case 13: return tb->perfCountersOut3_13;
            case 14: return tb->perfCountersOut3_14;
            case 15: return tb->perfCountersOut3_15;
            case 16: return tb->perfCountersOut3_16;
            case 17: return tb->perfCountersOut3_17;
            case 18: return tb->perfCountersOut3_18;
            case 19: return tb->perfCountersOut3_19;
            case 20: return tb->perfCountersOut3_20;
            case 21: return tb->perfCountersOut3_21;
            case 22: return tb->perfCountersOut3_22;
            case 23: return tb->perfCountersOut3_23;
            case 24: return tb->perfCountersOut3_24;
            case 25: return tb->perfCountersOut3_25;
            case 26: return tb->perfCountersOut3_26;
            case 29: return tb->perfCountersOut3_29;
            case 30: return tb->perfCountersOut3_30;
            case 31: return tb->perfCountersOut3_31;
            case 32: return tb->perfCountersOut3_32;
            case 33: return tb->perfCountersOut3_33;
            case 34: return tb->perfCountersOut3_34;
            case 35: return tb->perfCountersOut3_35;
            case 36: return tb->perfCountersOut3_36;
            case 37: return tb->perfCountersOut3_37;
            case 38: return tb->perfCountersOut3_38;
            case 39: return tb->perfCountersOut3_39;
            case 40: return tb->perfCountersOut3_40;
            default: return 0ULL; }
        default: return 0ULL;
        }
    }

    void read_counters(int c, uint64_t *r) const {
        for (int k = 0; k <= 40; ++k) r[k] = get_core_counter(c, k);
    }

    // Derive metrics from a counter snapshot, so the same maths serves both
    // the whole-run numbers and the work-region window below.
    PerfMetrics metrics_from(const uint64_t *r) const {
        PerfMetrics m;
        m.cycles            = r[0];
        m.inst_retired      = r[1];
        m.branch_total      = r[2];
        m.branches_passed   = r[3];
        m.scheduler_stalls  = r[4];
        m.rob_stalls        = r[5];
        m.decode_ready      = r[6];
        m.decode_fired      = r[7];
        m.icache_stalls     = r[8];
        m.dcache_reqs       = r[9];
        m.dcache_miss       = r[10];
        m.dcache_rd_beats   = r[11];
        m.dcache_wr_beats   = r[12];
        m.icache_miss       = r[13];
        m.icache_rd_beats   = r[14];
        m.l2_to_mem_rd_reqs = r[15];
        m.l2_to_mem_rd_beats= r[16];
        m.l2_to_mem_wr_beats= r[17];
        m.fe_fetch_not_ready  = r[18];
        m.fe_decode_not_ready = r[19];
        m.fe_expected_block   = r[20];
        m.ds_prf_exhausted    = r[21];
        m.ds_branch_mask_full = r[22];
        m.ds_rename_collide   = r[23];
        m.flush_branch        = r[24];
        m.flush_coherent      = r[25];
        m.retired_branch      = r[26];
        m.rob_head_not_ready  = r[29];
        m.rob_ready_blocked   = r[30];
        m.hnr_load            = r[31];
        m.hnr_branch          = r[32];
        m.hnr_mext            = r[33];
        m.hnr_amo             = r[34];
        m.hnr_other           = r[35];
        m.rnr_store_gate      = r[36];
        m.rnr_wb_gate         = r[37];
        m.rnr_load_gate       = r[38];
        m.fe_resp_valid_idle  = 0;  // no lineStreamer in quad-core fetch
        m.fe_cache_not_prod   = 0;
        m.fe_req_fire         = 0;
        m.fe_req_refused      = 0;

        const double clock_hz      = 75000000.0;
        const double bytes_per_beat = 8.0;
        uint64_t sc  = safe_max1(m.cycles);
        uint64_t sdr = safe_max1(m.decode_ready);
        uint64_t sbt = safe_max1(m.branch_total);
        uint64_t sdq = safe_max1(m.dcache_reqs);

        m.ipc = (double)m.inst_retired / (double)sc;
        m.ip_per_sec = m.ipc * clock_hz;
        m.branch_accuracy_pct  = 100.0 * (double)m.branches_passed / (double)sbt;
        m.icache_miss_rate_pct = 100.0 * (double)m.icache_miss / (double)sdr;
        m.dcache_miss_rate_pct = 100.0 * (double)m.dcache_miss / (double)sdq;
        m.l1d_read_bw_bytes_per_cycle  = (double)m.dcache_rd_beats * bytes_per_beat / (double)sc;
        m.l1d_write_bw_bytes_per_cycle = (double)m.dcache_wr_beats * bytes_per_beat / (double)sc;
        m.l1i_read_bw_bytes_per_cycle  = (double)m.icache_rd_beats * bytes_per_beat / (double)sc;
        m.dram_read_bw_MB_per_sec  = (double)m.l2_to_mem_rd_beats * bytes_per_beat / (double)sc * clock_hz / 1e6;
        m.dram_write_bw_MB_per_sec = (double)m.l2_to_mem_wr_beats * bytes_per_beat / (double)sc * clock_hz / 1e6;
        m.scheduler_stall_pct  = 100.0 * (double)m.scheduler_stalls / (double)sdr;
        m.rob_stall_pct        = 100.0 * (double)m.rob_stalls       / (double)sdr;
        m.decode_efficiency_pct = 100.0 * (double)m.decode_fired    / (double)sdr;
        m.dcache_mem_reqs_per_million_cycles = (double)m.dcache_reqs / (double)sc * 1e6;
        return m;
    }

    PerfMetrics compute_core_metrics(int c) const {
        uint64_t r[41]; read_counters(c, r); return metrics_from(r);
    }

    // Work-region metrics: the counters as they stood at the end of the last
    // block of commits that looked like real code. Falls back to the whole run
    // if this hart never spun (hart 0 normally never does).
    PerfMetrics compute_core_metrics_work(int c) const {
        if (!win[c].have_snap) return compute_core_metrics(c);
        return metrics_from(win[c].snap);
    }

    // Emit a JSON object with per-core breakdown and aggregate summary.
    // Aggregate IPC = total instructions / max-cycle (throughput from wall-clock).
    void print_json(const std::string &benchmark_name,
                    const std::string &output_path = "",
                    int harness_exit = 0,
                    int bench_error = -1) const {
        PerfMetrics c[4];
        for (int i = 0; i < 4; ++i) c[i] = compute_core_metrics(i);

        uint64_t agg_inst = 0;
        uint64_t agg_cyc  = 0;
        for (int i = 0; i < 4; ++i) {
            agg_inst += c[i].inst_retired;
            if (c[i].cycles > agg_cyc) agg_cyc = c[i].cycles;
        }
        uint64_t safe_agg = safe_max1(agg_cyc);
        double agg_ipc = (double)agg_inst / (double)safe_agg;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\n";
        ss << "  \"benchmark\": \"" << benchmark_name << "\",\n";
        ss << "  \"aggregate\": {\n";
        ss << "    \"total_inst_retired\": " << agg_inst << ",\n";
        ss << "    \"max_cycles\": "         << agg_cyc  << ",\n";
        ss << "    \"aggregate_ipc\": "      << agg_ipc  << "\n";
        ss << "  },\n";
        ss << "  \"result\": {\n";
        ss << "    \"complete\": "     << (harness_exit == 0 ? "true" : "false") << ",\n";
        ss << "    \"harness_exit\": " << harness_exit << ",\n";
        ss << "    \"bench_error\": "  << bench_error << "\n";
        ss << "  },\n";
        ss << "  \"cores\": [\n";
        for (int i = 0; i < 4; ++i) {
            const PerfMetrics &m = c[i];
            ss << "    {\n";
            ss << "      \"core\": "                  << i << ",\n";
            ss << "      \"raw\": {\n";
            ss << "        \"cycles\": "              << m.cycles            << ",\n";
            ss << "        \"inst_retired\": "        << m.inst_retired      << ",\n";
            ss << "        \"branch_total\": "        << m.branch_total      << ",\n";
            ss << "        \"branches_passed\": "     << m.branches_passed   << ",\n";
            ss << "        \"scheduler_stalls\": "    << m.scheduler_stalls  << ",\n";
            ss << "        \"rob_stalls\": "          << m.rob_stalls        << ",\n";
            ss << "        \"decode_ready\": "        << m.decode_ready      << ",\n";
            ss << "        \"decode_fired\": "        << m.decode_fired      << ",\n";
            ss << "        \"icache_stalls\": "       << m.icache_stalls     << ",\n";
            ss << "        \"dcache_reqs\": "         << m.dcache_reqs       << ",\n";
            ss << "        \"dcache_miss\": "         << m.dcache_miss       << ",\n";
            ss << "        \"dcache_rd_beats\": "     << m.dcache_rd_beats   << ",\n";
            ss << "        \"dcache_wr_beats\": "     << m.dcache_wr_beats   << ",\n";
            ss << "        \"icache_miss\": "         << m.icache_miss       << ",\n";
            ss << "        \"icache_rd_beats\": "     << m.icache_rd_beats   << ",\n";
            ss << "        \"l2_to_mem_rd_reqs\": "   << m.l2_to_mem_rd_reqs << ",\n";
            ss << "        \"l2_to_mem_rd_beats\": "  << m.l2_to_mem_rd_beats<< ",\n";
            ss << "        \"l2_to_mem_wr_beats\": "  << m.l2_to_mem_wr_beats<< ",\n";
            ss << "        \"fe_fetch_not_ready\": "  << m.fe_fetch_not_ready << ",\n";
            ss << "        \"fe_decode_not_ready\": " << m.fe_decode_not_ready<< ",\n";
            ss << "        \"fe_expected_block\": "   << m.fe_expected_block  << ",\n";
            ss << "        \"ds_prf_exhausted\": "    << m.ds_prf_exhausted   << ",\n";
            ss << "        \"ds_branch_mask_full\": " << m.ds_branch_mask_full<< ",\n";
            ss << "        \"ds_rename_collide\": "   << m.ds_rename_collide  << ",\n";
            ss << "        \"flush_branch\": "        << m.flush_branch       << ",\n";
            ss << "        \"flush_coherent\": "      << m.flush_coherent     << ",\n";
            ss << "        \"retired_branch\": "      << m.retired_branch     << ",\n";
            ss << "        \"rob_head_not_ready\": "  << m.rob_head_not_ready << ",\n";
            ss << "        \"rob_ready_blocked\": "   << m.rob_ready_blocked  << ",\n";
            ss << "        \"hnr_load\": "            << m.hnr_load           << ",\n";
            ss << "        \"hnr_branch\": "          << m.hnr_branch         << ",\n";
            ss << "        \"hnr_mext\": "            << m.hnr_mext           << ",\n";
            ss << "        \"hnr_amo\": "             << m.hnr_amo            << ",\n";
            ss << "        \"hnr_other\": "           << m.hnr_other          << ",\n";
            ss << "        \"rnr_store_gate\": "      << m.rnr_store_gate     << ",\n";
            ss << "        \"rnr_wb_gate\": "         << m.rnr_wb_gate        << ",\n";
            ss << "        \"rnr_load_gate\": "       << m.rnr_load_gate      << "\n";
            ss << "      },\n";
            // Work-region view: same counters, sampled at the end of the last
            // block of commits that looked like real code rather than spin.
            {
                PerfMetrics w = compute_core_metrics_work(i);
                uint64_t swc = safe_max1(w.cycles);
                ss << "      \"work\": {\n";
                ss << "        \"cycles\": "             << w.cycles            << ",\n";
                ss << "        \"inst_retired\": "       << w.inst_retired      << ",\n";
                ss << "        \"ipc\": "                << w.ipc               << ",\n";
                ss << "        \"spin_commit_pct\": "    << spin_commit_pct(i)  << ",\n";
                ss << "        \"spin_pc_lo\": "         << win[i].spin_lo      << ",\n";
                ss << "        \"spin_pc_hi\": "         << win[i].spin_hi      << ",\n";
                ss << "        \"rob_head_not_ready_pct\": " << 100.0*(double)w.rob_head_not_ready/(double)swc << ",\n";
                ss << "        \"hnr_load_pct\": "       << 100.0*(double)w.hnr_load/(double)swc       << ",\n";
                ss << "        \"hnr_mext_pct\": "       << 100.0*(double)w.hnr_mext/(double)swc       << ",\n";
                ss << "        \"hnr_other_pct\": "      << 100.0*(double)w.hnr_other/(double)swc      << ",\n";
                ss << "        \"rob_ready_blocked_pct\": " << 100.0*(double)w.rob_ready_blocked/(double)swc << ",\n";
                ss << "        \"rnr_store_gate_pct\": " << 100.0*(double)w.rnr_store_gate/(double)swc << ",\n";
                ss << "        \"icache_stall_pct\": "   << 100.0*(double)w.icache_stalls/(double)swc  << ",\n";
                ss << "        \"dcache_reqs\": "        << w.dcache_reqs       << ",\n";
                ss << "        \"dcache_miss\": "        << w.dcache_miss       << ",\n";
                ss << "        \"dcache_miss_rate_pct\": " << w.dcache_miss_rate_pct << ",\n";
                ss << "        \"dcache_rd_beats\": "    << w.dcache_rd_beats   << ",\n";
                ss << "        \"dcache_wr_beats\": "    << w.dcache_wr_beats   << ",\n";
                ss << "        \"retired_branch\": "     << w.retired_branch    << "\n";
                ss << "      },\n";
            }
            ss << "      \"derived\": {\n";
            ss << "        \"ipc\": "                           << m.ipc                           << ",\n";
            ss << "        \"branch_accuracy_pct\": "           << m.branch_accuracy_pct           << ",\n";
            ss << "        \"icache_miss_rate_pct\": "          << m.icache_miss_rate_pct          << ",\n";
            ss << "        \"dcache_miss_rate_pct\": "          << m.dcache_miss_rate_pct          << ",\n";
            ss << "        \"l1d_read_bw_bytes_per_cycle\": "   << m.l1d_read_bw_bytes_per_cycle   << ",\n";
            ss << "        \"l1d_write_bw_bytes_per_cycle\": "  << m.l1d_write_bw_bytes_per_cycle  << ",\n";
            ss << "        \"scheduler_stall_pct\": "           << m.scheduler_stall_pct           << ",\n";
            ss << "        \"rob_stall_pct\": "                 << m.rob_stall_pct                 << ",\n";
            ss << "        \"decode_efficiency_pct\": "         << m.decode_efficiency_pct         << "\n";
            ss << "      }\n";
            ss << "    }" << (i < 3 ? "," : "") << "\n";
        }
        ss << "  ]\n";
        ss << "}\n";

        std::string json_str = ss.str();
        printf("%s", json_str.c_str());

        if (!output_path.empty()) {
            std::ofstream out(output_path);
            if (out.is_open()) {
                out << json_str;
                out.close();
                printf("[profiler_quad] JSON written to: %s\n", output_path.c_str());
            } else {
                fprintf(stderr, "[profiler_quad] ERROR: cannot open: %s\n", output_path.c_str());
            }
        }
    }

    void print_summary() const {
        PerfMetrics c[4];
        for (int i = 0; i < 4; ++i) c[i] = compute_core_metrics(i);

        uint64_t agg_inst = 0, agg_cyc = 0;
        for (int i = 0; i < 4; ++i) {
            agg_inst += c[i].inst_retired;
            if (c[i].cycles > agg_cyc) agg_cyc = c[i].cycles;
        }
        double agg_ipc = (double)agg_inst / (double)safe_max1(agg_cyc);

        printf("\n========== Quad-Core Performance Summary ==========\n");
        printf("  Aggregate IPC:     %20.4f  (sum_insts / max_cycles)\n", agg_ipc);
        printf("  Total instructions:%20llu\n", (unsigned long long)agg_inst);
        printf("  Max cycles:        %20llu\n", (unsigned long long)agg_cyc);
        printf("\n  Core | Cycles         | Insts          | IPC    | D$miss%% | I$miss%%\n");
        printf("  -----|----------------|----------------|--------|---------|--------\n");
        for (int i = 0; i < 4; ++i) {
            const PerfMetrics &m = c[i];
            printf("    %d  | %14llu | %14llu | %6.4f | %6.2f%% | %6.2f%%\n",
                   i,
                   (unsigned long long)m.cycles,
                   (unsigned long long)m.inst_retired,
                   m.ipc,
                   m.dcache_miss_rate_pct,
                   m.icache_miss_rate_pct);
        }
        printf("\n  Core | Sched-stall%%  | ROB-stall%%   | Dec-eff%%  | BPred%%\n");
        printf("  -----|--------------|--------------|----------|--------\n");
        for (int i = 0; i < 4; ++i) {
            const PerfMetrics &m = c[i];
            printf("    %d  | %12.2f | %12.2f | %8.2f | %6.2f\n",
                   i, m.scheduler_stall_pct, m.rob_stall_pct,
                   m.decode_efficiency_pct, m.branch_accuracy_pct);
        }
        printf("====================================================\n\n");
    }
};
