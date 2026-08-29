// ccu_thruput_probe.cpp — measure where the coherent interconnect actually
// spends its cycles, so interconnect work is driven by data instead of by
// reading the FSMs and guessing.
//
// The CCU is three stages (dispatch FSM_3, eight snoop FSMs 4..11, response
// FSM_12) joined by barriers. This walks a quad-core benchmark and reports,
// per stage: a per-state cycle histogram, the transaction count, the mean
// service time, and stage occupancy as a fraction of the run.
//
// The number that decides what to optimise next is OCCUPANCY: if the response
// stage is busy only a small fraction of the run, the interconnect is
// LATENCY-bound and shaving dead states off the miss path is what pays; if it
// is near saturation it is THROUGHPUT-bound and only more beats per cycle
// (wider data, more transactions in flight) will move it.
//
// Build: make build/ccu_thruput_probe.out
// Run  : build/ccu_thruput_probe.out --image bins/mt-vvadd-s5-q4.bin \
//          --done-pc 0x8000099c --done-pc 0x800009a4 --done-pc 0x800009ac \
//          [--timeout N]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Vsystem.h"
#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"
#include "sim/harness/common/completion.h"

using namespace harness;

#define CCU(sig) tb->system__DOT__chiron__DOT__interconnect___DOT__CCU__DOT__##sig
#define FIFO(sig) tb->system__DOT__chiron__DOT__interconnect___DOT__FIFO__DOT__##sig

// FSM_3 (dispatch) and FSM_12 (response) both use few states; 16 covers the
// 4-bit snoop FSMs too.
static uint64_t h3[16], h12[16];
static uint64_t hs[8][16];   // the eight snoop FSMs, 4..11

int main(int argc, char *argv[]) {
    const char *image_path = find_arg(argc, argv, "--image", "bins/mt-vvadd-s5-q4.bin");
    const char *name       = find_arg(argc, argv, "--name", "ccu");
    uint64_t max_cycles = strtoull(find_arg(argc, argv, "--timeout", "120000000"), nullptr, 10);
    Completion completion = Completion::parse(argc, argv);

    Verilated::commandArgs(argc, argv);
    Vsystem *tb = new Vsystem;
    Verilated::traceEverOn(false);
    unsigned long long tickcount = 0ULL;
    reset(tb, tickcount);
    if (!load_image(tb, std::string(image_path), tickcount, "[ccu]", stdout, false)) {
        delete tb; return 2;
    }

    uint64_t cyc = 0, stall = 0;
    // A transaction is one pass of the response stage: FSM_12 leaving IDLE.
    uint64_t txns = 0, resp_busy = 0, disp_busy = 0, snoop_busy = 0;
    uint64_t resp_service = 0;      // cycles from leaving IDLE to returning
    // Is the interconnect the bottleneck, or is it starved? These separate the
    // two: `overlap` counts cycles with more than one stage busy (what the
    // pipelining is supposed to buy), and `backlog` counts cycles where a
    // request was already queued while the CCU was busy -- work that pipelining
    // could actually absorb. If backlog is near zero the CCU is request-starved
    // and only its LATENCY matters, not its throughput.
    uint64_t any_busy = 0, overlap = 0, backlog = 0, q_nonempty = 0;
    uint64_t fifo_occ = 0;
    // Snoop-filter headroom: how many transactions had NO peer holding the
    // line at all (every crpbuf_3_* came back zero)? Those are the ones a
    // directory could answer without a broadcast, skipping the whole snoop
    // stage. This is the CEILING on what a snoop filter can buy.
    uint64_t no_sharer = 0, any_data = 0;
    // The filter's verdict, sampled once per transaction: FSM_3 sits in SNOOP
    // (state 6) for exactly one cycle per dispatch, and sfPeers is the mask of
    // masters the directory says may hold the line.
    uint64_t sf_skip = 0, sf_bcast = 0, sf_masters = 0;
    // Split the non-skips: 0xff means the set is POISONED (it overflowed, so
    // the directory refuses to answer and everyone gets snooped); anything
    // else is real recorded presence. That distinguishes "the filter is
    // working but the line really is shared" from "the directory gave up".
    uint64_t sf_poison = 0, sf_real = 0;
    int prev3 = 0;
    int prev12 = 0;
    int exit_code = 2;

    while (cyc < max_cycles) {
        tick_nodump(tb);
        ++tickcount; ++cyc;

        int s3  = (int)CCU(stateReg_3)  & 15;
        int s12 = (int)CCU(stateReg_12) & 15;
        int sn[8] = { (int)CCU(stateReg_4)&15,  (int)CCU(stateReg_5)&15,
                      (int)CCU(stateReg_6)&15,  (int)CCU(stateReg_7)&15,
                      (int)CCU(stateReg_8)&15,  (int)CCU(stateReg_9)&15,
                      (int)CCU(stateReg_10)&15, (int)CCU(stateReg_11)&15 };
        h3[s3]++; h12[s12]++;
        if (s3 == 6 && prev3 != 6) {
            unsigned pm = (unsigned)CCU(sfPeers) & 0xffu;
            if (pm == 0) sf_skip++; else { sf_bcast++; if (pm == 0xffu) sf_poison++; else sf_real++; }
            sf_masters += __builtin_popcount(pm);
        }
        prev3 = s3;
        for (int i = 0; i < 8; ++i) hs[i][sn[i]]++;
        int s4 = sn[0];
        if (s3  != 0) disp_busy++;
        if (s4  != 0) snoop_busy++;
        if (s12 != 0) { resp_busy++; resp_service++; }
        int nbusy = (s3 != 0) + (s4 != 0) + (s12 != 0);
        // the other seven snoop FSMs move in lockstep with FSM_4, so counting
        // core0's is enough to say "the snoop stage is busy"
        if (nbusy)      any_busy++;
        if (nbusy > 1)  overlap++;
        int rp = (int)FIFO(readPtr), wp = (int)FIFO(writePtr);
        bool empty = FIFO(emptyReg) != 0;
        int occ = empty ? 0 : ((wp - rp + 32) % 32 ? (wp - rp + 32) % 32 : 32);
        fifo_occ += occ;
        if (!empty) q_nonempty++;
        if (!empty && nbusy) backlog++;
        if (prev12 == 0 && s12 != 0) {
            txns++;
            // Only the even ports (the D-caches) can ever answer non-zero:
            // ICache.scala hardwires CRRESP := 0, which is why Verilator folds
            // crpbuf_3_{1,3,5,7} away entirely. So these four ARE the response.
            unsigned crp[4] = { (unsigned)CCU(crpbuf_3_0), (unsigned)CCU(crpbuf_3_2),
                                (unsigned)CCU(crpbuf_3_4), (unsigned)CCU(crpbuf_3_6) };
            unsigned all = 0, data = 0;
            for (int k = 0; k < 4; ++k) { all |= crp[k]; data |= (crp[k] & 1u); }
            if (!all)  no_sharer++;
            if (data)  any_data++;
        }
        prev12 = s12;

        if (tb->core0OutChar_valid) { putchar(tb->core0OutChar_byte); }
        if (tb->core1OutChar_valid) { putchar(tb->core1OutChar_byte); }
        if (tb->core2OutChar_valid) { putchar(tb->core2OutChar_byte); }
        if (tb->core3OutChar_valid) { putchar(tb->core3OutChar_byte); }

        if (!tb->robOut0_commitFired) {
            if (++stall >= 500000ULL) {
                printf("\n[ccu] DEADLOCK at C0=0x%llx\n", (unsigned long long)tb->robOut0_pc);
                exit_code = 3; break;
            }
            continue;
        }
        stall = 0;
        if (completion.active() && completion.hit(tb->robOut0_pc, tb->registersOut0_10)) {
            printf("\n[ccu] COMPLETE at 0x%llx\n", (unsigned long long)tb->robOut0_pc);
            exit_code = 0; break;
        }
    }

    printf("\n===== CCU throughput: %s =====\n", name);
    printf("cycles              %12llu\n", (unsigned long long)cyc);
    printf("transactions        %12llu\n", (unsigned long long)txns);
    if (txns) {
        printf("mean service        %12.2f cycles/txn (response stage)\n",
               (double)resp_service / (double)txns);
        printf("mean dispatch       %12.2f cycles/txn\n", (double)disp_busy / (double)txns);
        printf("mean snoop          %12.2f cycles/txn\n", (double)snoop_busy / (double)txns);
    }
    printf("CCU busy (any stage) %10.2f%%     stages overlapping %6.2f%% of busy\n",
           100.0 * any_busy / (cyc ? cyc : 1),
           100.0 * overlap / (any_busy ? any_busy : 1));
    printf("request queue non-empty %7.2f%%   backlog while busy %6.2f%%   mean depth %.3f\n",
           100.0 * q_nonempty / (cyc ? cyc : 1),
           100.0 * backlog / (cyc ? cyc : 1),
           (double)fifo_occ / (cyc ? cyc : 1));
    printf("snoop filter: %llu txns fully skipped, %llu broadcast, %.2f%% skipped;"
           " mean masters snooped %.2f of 8\n",
           (unsigned long long)sf_skip, (unsigned long long)sf_bcast,
           100.0 * sf_skip / ((sf_skip + sf_bcast) ? (sf_skip + sf_bcast) : 1),
           (double)sf_masters / ((sf_skip + sf_bcast) ? (sf_skip + sf_bcast) : 1));
    printf("  of the broadcasts: %llu from a POISONED set (%.2f%% of all txns),"
           " %llu from real presence\n",
           (unsigned long long)sf_poison,
           100.0 * sf_poison / ((sf_skip + sf_bcast) ? (sf_skip + sf_bcast) : 1),
           (unsigned long long)sf_real);
    printf("snoop-filter ceiling: %.2f%% of txns had NO peer response at all;"
           " %.2f%% got data from a peer\n",
           100.0 * no_sharer / (txns ? txns : 1), 100.0 * any_data / (txns ? txns : 1));
    printf("occupancy  response  %11.2f%%   dispatch %6.2f%%   snoop %6.2f%%\n",
           100.0 * resp_busy / (cyc ? cyc : 1),
           100.0 * disp_busy / (cyc ? cyc : 1),
           100.0 * snoop_busy / (cyc ? cyc : 1));

    auto dump = [&](const char *tag, uint64_t *h, const char **names) {
        printf("\n%s per-state cycles:\n", tag);
        for (int i = 0; i < 16; ++i)
            if (h[i])
                printf("  s%-2d %-22s %12llu  %6.2f%%  %8.2f/txn\n", i,
                       names[i] ? names[i] : "",
                       (unsigned long long)h[i], 100.0 * h[i] / (cyc ? cyc : 1),
                       txns ? (double)h[i] / (double)txns : 0.0);
    };
    static const char *n3[16]  = {"IDLE","AR retry","BUFF+DEQ",0,"SYNC",0,"SNOOP",0,0,0,0,0,0,0,0,0};
    static const char *n4[16]  = {"IDLE","CA","CR",0,"FINISH barrier","SYNC(wait resp)",0,"RSP","addr interlock",0,0,0,0,0,0,0};
    static const char *n12[16] = {"IDLE","","CAPTURE",0,0,"RSP beat","RSP_ARBAR",0,0,0,0,0,0,0,0,0};
    dump("FSM_3  dispatch", h3, n3);
    dump("FSM_12 response", h12, n12);

    // Per-snooper CA+CR time is what sets the 8-way FINISH barrier: every
    // snoop FSM sits in FINISH until the SLOWEST of them has answered, so the
    // barrier column below is the cost of the worst responder, not of this one.
    printf("\nsnoop FSMs (cycles/txn): master  CA    CR   FINISHwait  SYNCwait  interlock  busy\n");
    for (int i = 0; i < 8; ++i) {
        double d = txns ? (double)txns : 1.0;
        uint64_t busy = 0;
        for (int s = 1; s < 16; ++s) busy += hs[i][s];
        printf("                          %d    %5.2f %5.2f  %8.2f  %8.2f  %9.2f %6.2f\n",
               i, hs[i][1]/d, hs[i][2]/d, hs[i][4]/d, hs[i][5]/d, hs[i][8]/d, busy/d);
    }

    delete tb;
    return exit_code;
}
