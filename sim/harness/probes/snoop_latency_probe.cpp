// snoop_latency_probe.cpp — where does a peer D-cache spend the ~4.5 cycles
// between accepting a snoop (AC) and answering it (CR)?
//
// ccu_thruput_probe showed the CCU's 8-way FINISH barrier costs ~6.8
// cycles/txn because it waits for the SLOWEST snoop response, and that the
// slow responders are the peer D-caches (ACE ports 2/4/6) at CR ~4.5 cycles
// while the I-caches answer in 1. This drills into that: it histograms each
// D-cache's snoop FSM (ACEUnit.coherentAXIState) so the cost is attributed to
// a state instead of guessed at.
//
// States, in order (ACEUnit.scala):
//   0 idle          accept AC here; ACREADY is asserted ONLY in this state
//   1 requestWait   decide writeback-pipe hit vs tag lookup; raise buffer.valid
//   2 requestIn     present to the D$ arbiter, wait for the tag lookup result
//   3 response      CRVALID
//   4 dataOut       CD beats (only when this cache supplies the line)
//
// Build: make build/snoop_latency_probe.out
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "Vsystem.h"
#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"
#include "sim/harness/common/completion.h"

using namespace harness;

#define ACE(n, sig) tb->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__aceUnit__DOT__##sig

static uint64_t hist[4][8];       // per core, per snoop-FSM state
static uint64_t snoops[4];        // AC accepted (idle -> requestWait)
static uint64_t ac_to_cr[4];      // cycles from leaving idle to reaching response
static uint64_t supplied[4];      // snoops this cache answered WITH data

int main(int argc, char *argv[]) {
    const char *image_path = find_arg(argc, argv, "--image", "bins/mt-csaxpy-s5-q4.bin");
    const char *name       = find_arg(argc, argv, "--name", "snoop");
    uint64_t max_cycles = strtoull(find_arg(argc, argv, "--timeout", "120000000"), nullptr, 10);
    Completion completion = Completion::parse(argc, argv);

    Verilated::commandArgs(argc, argv);
    Vsystem *tb = new Vsystem;
    Verilated::traceEverOn(false);
    unsigned long long tickcount = 0ULL;
    reset(tb, tickcount);
    if (!load_image(tb, std::string(image_path), tickcount, "[snoop]", stdout, false)) {
        delete tb; return 2;
    }

    uint64_t cyc = 0, stall = 0;
    int prev[4] = {0, 0, 0, 0};
    uint64_t started[4] = {0, 0, 0, 0};
    bool inflight[4] = {false, false, false, false};
    int exit_code = 2;

    while (cyc < max_cycles) {
        tick_nodump(tb); ++tickcount; ++cyc;

        int s[4] = { (int)ACE(0, coherentAXIState) & 7, (int)ACE(1, coherentAXIState) & 7,
                     (int)ACE(2, coherentAXIState) & 7, (int)ACE(3, coherentAXIState) & 7 };
        int dv[4] = { (int)ACE(0, coherencyResponseBuffer_dataValid), (int)ACE(1, coherencyResponseBuffer_dataValid),
                      (int)ACE(2, coherencyResponseBuffer_dataValid), (int)ACE(3, coherencyResponseBuffer_dataValid) };
        for (int i = 0; i < 4; ++i) {
            hist[i][s[i]]++;
            if (prev[i] == 0 && s[i] == 1) { snoops[i]++; started[i] = cyc; inflight[i] = true; }
            // CR completes when the FSM leaves requestIn(2) -- either straight
            // to dataOut/idle (the fast path, CRREADY already up) or via
            // response(3) when the CCU was not ready. Anchoring on "left
            // requestIn" keeps this valid for both.
            if (inflight[i] && prev[i] == 2 && s[i] != 2) {
                ac_to_cr[i] += cyc - started[i];
                if (dv[i]) supplied[i]++;
                inflight[i] = false;
            }
            prev[i] = s[i];
        }

        if (tb->core0OutChar_valid) putchar(tb->core0OutChar_byte);
        if (tb->core1OutChar_valid) putchar(tb->core1OutChar_byte);
        if (tb->core2OutChar_valid) putchar(tb->core2OutChar_byte);
        if (tb->core3OutChar_valid) putchar(tb->core3OutChar_byte);

        if (!tb->robOut0_commitFired) {
            if (++stall >= 500000ULL) {
                printf("\n[snoop] DEADLOCK at C0=0x%llx\n", (unsigned long long)tb->robOut0_pc);
                exit_code = 3; break;
            }
            continue;
        }
        stall = 0;
        if (completion.active() && completion.hit(tb->robOut0_pc, tb->registersOut0_10)) {
            printf("\n[snoop] COMPLETE\n"); exit_code = 0; break;
        }
    }

    printf("\n===== D-cache snoop latency: %s =====\n", name);
    printf("cycles %llu\n\n", (unsigned long long)cyc);
    printf("core  snoops  AC->CR   supplied |  idle    reqWait  requestIn  response  dataOut   (cycles/snoop)\n");
    for (int i = 0; i < 4; ++i) {
        double n = snoops[i] ? (double)snoops[i] : 1.0;
        printf("  %d %8llu  %6.2f  %8llu | %7.2f %8.2f %10.2f %9.2f %8.2f\n", i,
               (unsigned long long)snoops[i], ac_to_cr[i] / n,
               (unsigned long long)supplied[i],
               hist[i][0] / n, hist[i][1] / n, hist[i][2] / n, hist[i][3] / n, hist[i][4] / n);
    }
    printf("\n(AC->CR is the number the CCU's FINISH barrier waits on. `supplied`\n"
           " counts snoops answered WITH data -- the rest cost the barrier the same\n"
           " latency while contributing nothing but a \"no\" .)\n");
    delete tb;
    return exit_code;
}
