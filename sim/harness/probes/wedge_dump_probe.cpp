// wedge_dump_probe.cpp — boot a Linux image RTL-only and, around a wedge
// window (all-harts PC freeze near cycle 65.5M in the linux-q4 boot), dump
// the full coherency-path state every SNAP_STEP cycles: CCU FSM_1..12 +
// pipeline buffers, each core's ACE-unit FSMs and request/response buffers,
// the interconnect ring FIFO, and the L2 (front ROB / MSHR / writeback
// buffer / AXI handshakes). Built to identify which FSM a 3-hart load
// deadlock is stuck in.
//
// Build:  make build/wedge_dump_probe.out
// Run  :  build/wedge_dump_probe.out bins/linux-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CCU(sig) tb_->system__DOT__chiron__DOT__interconnect___DOT__CCU__DOT__##sig
#define LLC(sig) tb_->system__DOT__chiron__DOT__LLC__DOT__##sig
#define ICN(sig) tb_->system__DOT__chiron__DOT__interconnect___DOT__##sig

#define CORE_SIG(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__memAccess__DOT__##sig
#define CORE_RAW(n, sig) tb_->system__DOT__chiron__DOT__core##n##__DOT__##sig
#define DUMP_CORE(n)                                                            \
  printf("[wedge]  core%d ace: cohAXI=%d rdReq=%d rdRsp=%d wrACE=%d "           \
         "cohReq(v=%d a=0x%llx r=%d) cohRsp(v=%d dv=%d) rdBuf(v=%d a=0x%llx) "  \
         "fence=%d subRdy=%d lookupRdy=%d schedOut(v=%d a=0x%llx)\n",           \
         n,                                                                     \
         (int)CORE_SIG(n, aceUnit__DOT__coherentAXIState),                      \
         (int)CORE_SIG(n, aceUnit__DOT__readACERequestState),                   \
         (int)CORE_SIG(n, aceUnit__DOT__readACEResponseState),                  \
         (int)CORE_SIG(n, aceUnit__DOT__writeACEState),                         \
         (int)CORE_SIG(n, aceUnit__DOT__coherencyRequestBuffer_valid),          \
         (unsigned long long)CORE_SIG(n, aceUnit__DOT__coherencyRequestBuffer_address), \
         (int)CORE_SIG(n, aceUnit__DOT__coherencyRequestBuffer_response),       \
         (int)CORE_SIG(n, aceUnit__DOT__coherencyResponseBuffer_valid),         \
         (int)CORE_SIG(n, aceUnit__DOT__coherencyResponseBuffer_dataValid),     \
         (int)CORE_SIG(n, aceUnit__DOT__readBuffer_valid),                      \
         (unsigned long long)CORE_SIG(n, aceUnit__DOT__readBuffer_address),     \
         (int)CORE_SIG(n, fenceState),                                          \
         (int)CORE_SIG(n, subModulesReady),                                     \
         (int)CORE_SIG(n, cacheLookup_request_ready),                           \
         (int)CORE_SIG(n, requestScheduler_requestOut_valid),                   \
         (unsigned long long)CORE_SIG(n, requestScheduler_requestOut_address)); \
  printf("[wedge]  core%d arb: atomicBusy=%d opState=%d "                       \
         "inord(v=%d wd=%d i=%08x a=0x%llx) op(v=%d i=%08x a=0x%llx) "          \
         "inordPend=%d holdInOrd=%d "                                           \
         "replay(waitEmpty=%d wbEmpty=%d w0(v=%d i=%08x a=0x%llx))\n",          \
         n,                                                                     \
         (int)CORE_SIG(n, arbiter__DOT__atomicBusyState),                       \
         (int)CORE_SIG(n, arbiter__DOT__operationState),                        \
         (int)CORE_SIG(n, arbiter__DOT__inorderBuffer_valid),                   \
         (int)CORE_SIG(n, arbiter__DOT__inorderBuffer_writeData_valid),         \
         (unsigned)CORE_SIG(n, arbiter__DOT__inorderBuffer_core_instruction),   \
         (unsigned long long)CORE_SIG(n, arbiter__DOT__inorderBuffer_address),  \
         (int)CORE_SIG(n, arbiter__DOT__operationBuffer_valid),                 \
         (unsigned)CORE_SIG(n, arbiter__DOT__operationBuffer_core_instruction), \
         (unsigned long long)CORE_SIG(n, arbiter__DOT__operationBuffer_address),\
         (int)CORE_SIG(n, cacheLookup_request_inorderPending),                  \
         (int)CORE_SIG(n, cacheLookup_request_holdInOrder),                     \
         (int)CORE_SIG(n, replayUnit__DOT__requestWaitFIFO__DOT__emptyReg),     \
         (int)CORE_SIG(n, replayUnit__DOT__writeBackFIFO__DOT__emptyReg),       \
         (int)CORE_SIG(n, replayUnit__DOT__requestWaitFIFO__DOT__memReg_0_valid), \
         (unsigned)CORE_SIG(n, replayUnit__DOT__requestWaitFIFO__DOT__memReg_0_core_instruction), \
         (unsigned long long)CORE_SIG(n, replayUnit__DOT__requestWaitFIFO__DOT__memReg_0_address)); \
  printf("[wedge]  core%d rob: rd=%d wr=%d empty=%d full=%d cmtRdy=%d cmtFired=%d " \
         "wC=%d wICrdy=%d wICfired=%d wcib=%d canIrq=%d canSoftIrq=%d "         \
         "injSoft=%d injSt=%d mip=%llx mie=%llx mstatus=%llx mepc=%llx\n",      \
         n,                                                                     \
         (int)CORE_RAW(n, rob__DOT__fifo__DOT__readPtr),                        \
         (int)CORE_RAW(n, rob__DOT__fifo__DOT__writePtr),                       \
         (int)CORE_RAW(n, rob__DOT__fifo__DOT__emptyReg),                       \
         (int)CORE_RAW(n, rob__DOT__fifo__DOT__fullReg),                        \
         (int)CORE_RAW(n, rob_commit_ready),                                    \
         (int)CORE_RAW(n, rob_commit_fired),                                    \
         (int)CORE_RAW(n, memAccess_writeCommit_fired),                         \
         (int)CORE_RAW(n, memAccess_writeInstructionCommit_ready),              \
         (int)CORE_RAW(n, memAccess_writeInstructionCommit_fired),              \
         (int)CORE_SIG(n, cacheLookup__DOT__writeCommitInstructionBuffer),      \
         (int)CORE_RAW(n, decode_canTakeInterrupt),                             \
         (int)CORE_RAW(n, decode_canTakeSoftInterrupt),                         \
         (int)CORE_RAW(n, injectingSoftwareInterrupt),                          \
         (int)CORE_RAW(n, interruptInjectStatus),                               \
         (unsigned long long)CORE_RAW(n, decode__DOT__mip),                     \
         (unsigned long long)CORE_RAW(n, decode__DOT__mie),                     \
         (unsigned long long)CORE_RAW(n, decode__DOT__mstatus),                 \
         (unsigned long long)CORE_RAW(n, decode__DOT__mepc))

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/linux-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  // Window env-overridable (WEDGE_FROM/WEDGE_DENSE/WEDGE_END) so the probe
  // can be retargeted at any wedge without a rebuild.
  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t SNAP_FROM  = env64("WEDGE_FROM",  65600000ULL);
  const uint64_t SNAP_DENSE = env64("WEDGE_DENSE", 66000000ULL);
  const uint64_t SNAP_END   = env64("WEDGE_END",   SNAP_DENSE + 1);
  const uint64_t STEP_DENSE = 100000ULL;
  const uint64_t STEP_COARSE= 1000000ULL;

  uint64_t cyc = 0, next_snap = SNAP_FROM;
  while (cyc < SNAP_END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if ((cyc % 10000000ULL) == 0) {
      printf("[wedge] progress cyc=%llu pcs=%llx/%llx/%llx/%llx\n",
             (unsigned long long)cyc,
             (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
             (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
      fflush(stdout);
    }
    if (cyc < next_snap) continue;
    next_snap += (cyc < SNAP_DENSE) ? STEP_DENSE : STEP_COARSE;

    printf("[wedge] ==== SNAP cyc=%llu pcs=%llx/%llx/%llx/%llx ====\n",
           (unsigned long long)cyc,
           (unsigned long long)tb_->robOut0_pc, (unsigned long long)tb_->robOut1_pc,
           (unsigned long long)tb_->robOut2_pc, (unsigned long long)tb_->robOut3_pc);
    printf("[wedge]  ccu s={%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d} "
           "p1(a=0x%llx t=%x c=%d) p2(a=0x%llx t=%x c=%d) p3(a=0x%llx t=%x c=%d) "
           "sel=%d crp2={%02x,%02x,%02x,%02x} crp3={%02x,%02x,%02x,%02x} wb=%d\n",
           (int)CCU(stateReg_1), (int)CCU(stateReg_2), (int)CCU(stateReg_3),
           (int)CCU(stateReg_4), (int)CCU(stateReg_5), (int)CCU(stateReg_6),
           (int)CCU(stateReg_7), (int)CCU(stateReg_8), (int)CCU(stateReg_9),
           (int)CCU(stateReg_10), (int)CCU(stateReg_11), (int)CCU(stateReg_12),
           (unsigned long long)CCU(addr_pbuf_1), (unsigned)CCU(tran_pbuf_1), (int)CCU(core_id_pbuf_1),
           (unsigned long long)CCU(addr_pbuf_2), (unsigned)CCU(tran_pbuf_2), (int)CCU(core_id_pbuf_2),
           (unsigned long long)CCU(addr_pbuf_3), (unsigned)CCU(tran_pbuf_3), (int)CCU(core_id_pbuf_3),
           (int)CCU(select_buff),
           (unsigned)CCU(crpbuf_2_0), (unsigned)CCU(crpbuf_2_2),
           (unsigned)CCU(crpbuf_2_4), (unsigned)CCU(crpbuf_2_6),
           (unsigned)CCU(crpbuf_3_0), (unsigned)CCU(crpbuf_3_2),
           (unsigned)CCU(crpbuf_3_4), (unsigned)CCU(crpbuf_3_6),
           (int)CCU(write_back));
    printf("[wedge]  ring rd=%d wr=%d empty=%d full=%d arb=%d\n",
           (int)ICN(FIFO__DOT__readPtr), (int)ICN(FIFO__DOT__writePtr),
           (int)ICN(FIFO__DOT__emptyReg), (int)ICN(FIFO__DOT__fullReg),
           (int)ICN(Arbiter__DOT__stateReg));
    DUMP_CORE(0); DUMP_CORE(1); DUMP_CORE(2); DUMP_CORE(3);
    printf("[wedge]  l2 rob(in=%d out=%d rcnt=%d wcnt=%d) mshr(st=%d rp=%d wp=%d "
           "ARV=%d RRDY=%d replhit=%d outRdy=%d) wbb(st=%d rp=%d wp=%d AWV=%d WV=%d) "
           "cache(inRdy=%d inFired=%d missRdy=%d missFired=%d missA=0x%llx hitRdy=%d hitFired=%d) "
           "cnt=%d replcnt=%d replfull=%d\n",
           (int)LLC(l2_front_Rob__DOT__inputBufferState), (int)LLC(l2_front_Rob__DOT__outputBufferState),
           (int)LLC(l2_front_Rob__DOT__axi_RDATA_counter), (int)LLC(l2_front_Rob__DOT__axi_WDATA_counter),
           (int)LLC(MSHR__DOT__state),
           (int)LLC(MSHR__DOT__enq_fifo__DOT__readPtr), (int)LLC(MSHR__DOT__enq_fifo__DOT__writePtr),
           (int)LLC(MSHR_io_axi_ARVALID), (int)LLC(MSHR_io_axi_RREADY),
           (int)LLC(MSHR_io_Mem_read_in_repl_hit), (int)LLC(MSHR_io_Mem_read_out_ready),
           (int)LLC(writeBackBuffer__DOT__state),
           (int)LLC(writeBackBuffer__DOT__fifo__DOT__readPtr), (int)LLC(writeBackBuffer__DOT__fifo__DOT__writePtr),
           (int)LLC(writeBackBuffer_io_axi_AWVALID), (int)LLC(writeBackBuffer_io_axi_WVALID),
           (int)LLC(cache_io_cache_in_ready), (int)LLC(cache_io_cache_in_fired),
           (int)LLC(cache_io_cache_miss_out_ready), (int)LLC(cache_io_cache_miss_out_fired),
           (unsigned long long)LLC(cache_io_cache_miss_out_Mem_addr),
           (int)LLC(cache_io_cache_hit_out_ready), (int)LLC(cache_io_cache_hit_out_fired),
           (int)LLC(count), (int)LLC(replace_count), (int)LLC(replace_full));
    fflush(stdout);
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
