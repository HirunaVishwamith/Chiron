// store_gate_probe.cpp — where do the store-gate cycles actually go?
//
// rnr_store_gate says the ROB head was a store that could not retire, and it
// is 100% of rob_ready_blocked on every core of every benchmark. The open
// question is what a post-commit store buffer would actually buy, because a
// buffer only helps the cycles the arbiter spends BUSY WITH THE PREVIOUS
// STORE. Cycles spent waiting for this store's own address to arrive, or for
// its data, are not recoverable that way.
//
// Method: perfCountersOut<c>_36 is the rnr_store_gate counter. Sampling it
// every cycle and watching for an increment marks exactly the cycles the gate
// counted, with no RTL change at all. Each such cycle is then attributed to
// the D-cache arbiter's operationState (arbiter.scala:115):
//
//   0 idle                     accepting; store not yet presented
//   1 commitReady              offering writeCommit.ready, store has not fired
//   2 commitFired              fired, waiting on store data
//   3 wait                     atomic read-pass outstanding
//   4 writeInstructionFired    BUSY WITH THE PREVIOUS STORE  <-- buffer target
//
// So (state 4) + (state 2/3 held by an older op) bounds what decoupling retire
// from the cache write can recover; state 0/1 does not.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "Vsystem.h"
#include "sim/harness/common/args.h"
#include "sim/harness/common/image.h"
#include "sim/harness/common/completion.h"

using namespace harness;

#define ARB(n, sig) tb->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__arbiter__DOT__##sig

static const char *SN[5] = {"idle", "commitReady", "commitFired", "wait", "writeInstrFired"};

int main(int argc, char *argv[]) {
    const char *image_path = find_arg(argc, argv, "--image", "bins/mt-vvadd-s5-q4.bin");
    const char *name       = find_arg(argc, argv, "--name", "storegate");
    uint64_t max_cycles = strtoull(find_arg(argc, argv, "--timeout", "120000000"), nullptr, 10);

    Completion completion = Completion::parse(argc, argv);

    Verilated::commandArgs(argc, argv);
    Vsystem *tb = new Vsystem;
    Verilated::traceEverOn(false);
    unsigned long long tickcount = 0ULL;
    reset(tb, tickcount);
    if (!load_image(tb, std::string(image_path), tickcount, "[storegate]", stdout, false)) {
        delete tb; return 2;
    }

    uint64_t hist[4][5]   = {};   // gate cycles by arbiter state
    uint64_t opbusy[4][5] = {};   // of those, a request still parked in the arbiter
    uint64_t prev36[4]    = {};
    uint64_t gate[4]      = {};
    uint64_t stall = 0;
    int exit_code = 2;

    uint64_t cyc = 0;
    while (cyc < max_cycles) {
        tick_nodump(tb); ++tickcount; ++cyc;

        uint64_t c36[4] = { tb->perfCountersOut0_36, tb->perfCountersOut1_36,
                            tb->perfCountersOut2_36, tb->perfCountersOut3_36 };
        uint32_t st[4]  = { ARB(0, operationState), ARB(1, operationState),
                            ARB(2, operationState), ARB(3, operationState) };
        uint32_t iv[4]  = { ARB(0, inorderBuffer_valid), ARB(1, inorderBuffer_valid),
                            ARB(2, inorderBuffer_valid), ARB(3, inorderBuffer_valid) };

        for (int c = 0; c < 4; ++c) {
            if (cyc && c36[c] != prev36[c]) {     // this cycle the gate counted
                uint32_t s = st[c] < 5 ? st[c] : 0;
                hist[c][s]++; gate[c]++;
                if (iv[c]) opbusy[c][s]++;        // a request still parked in the arbiter
            }
            prev36[c] = c36[c];
        }

        if (tb->core0OutChar_valid) putchar(tb->core0OutChar_byte);
        if (!tb->robOut0_commitFired) {
            if (++stall >= 500000ULL) {
                printf("\n[storegate] DEADLOCK at C0=0x%llx\n",
                       (unsigned long long)tb->robOut0_pc);
                exit_code = 3; break;
            }
            continue;
        }
        stall = 0;
        if (completion.active() && completion.hit(tb->robOut0_pc, tb->registersOut0_10)) {
            printf("\n[storegate] COMPLETE\n"); exit_code = 0; break;
        }
    }

    printf("\n=== store gate attribution: %s (%llu cycles) ===\n",
           name, (unsigned long long)cyc);
    for (int c = 0; c < 4; ++c) {
        if (!gate[c]) continue;
        printf("core%d  gate=%llu\n", c, (unsigned long long)gate[c]);
        for (int s = 0; s < 5; ++s) {
            if (!hist[c][s]) continue;
            printf("    %-16s %9llu  %5.1f%%   (inorderBuffer busy %5.1f%%)\n",
                   SN[s], (unsigned long long)hist[c][s],
                   100.0 * hist[c][s] / gate[c],
                   hist[c][s] ? 100.0 * opbusy[c][s] / hist[c][s] : 0.0);
        }
        double recoverable = 100.0 * (hist[c][4] + hist[c][2] + hist[c][3]) / gate[c];
        printf("    -> buffer-recoverable (states 2/3/4): %.1f%%\n", recoverable);
    }
    delete tb;
    return exit_code;
}
