// linux_walkerdata_probe — the fence.i walker writes back the RIGHT address
// with the WRONG (zero) data. Find out why.
//
// Established by linux_tramp_probe on the Linux boot (hart0, line 0x81b03840):
//
//   1207810343  walkerWB h0 HOLDS watched line retain=1 word0=00000000  <- walker
//   1230905029  ifill  h0 LANDS for 81b03840  word0=00000000            <- fetch gets zeros
//   1231390067  evictWB h0 HOLDS watched line word0=08b00893            <- eviction: CORRECT
//
// So the D-cache data array holds the correct 0x08b00893 the whole time; the
// normal eviction path reads it correctly, and only the walker's capture reads
// zeros. It happened on all four sweeps, so it is structural, not a rare race.
//
// The walker captures in sFlushCapture:
//     flushTagReg  := tagBRAM.rdData          (correct — address came out right)
//     flushDataReg := dataBRAMVec.map(_.rdData)   (zeros)
// having driven rdAddr := flushSet one cycle earlier, guarded by `bramFree`,
// which is the exact negation of the request path's own rdAddr condition. Tag
// and data are the same module type (moduleForwardingMemory) on the same
// address in the same cycle, so the divergence must be in the read/forward path:
//
//   val memData = mem.read(rdAddr)                        // unconditional
//   val doForwardReg = RegNext(wrAddr === rdAddr && wrEna)
//   rdData := Mux(doForwardReg, wrDataReg, memData)
//
// This probe prints, cycle by cycle across the walker's pass over the watched
// set: the FSM state, both BRAMs' rdAddr/wrEna/wrAddr/doForwardReg, the data
// actually returned, AND the memory array's own contents at that set. Comparing
// rdData against mem[set] settles whether the array was read wrong or the
// forward mux hijacked the result — and `mem_memData_addr_pipe_0` (the
// SyncReadMem's latched read address) shows whether the walker's address even
// reached the memory.
//
// Run (ckpt 1.2B is ~7.8M cycles before the first sweep, ~25 min):
//   CKPT_RESTORE=ckpt/ckpt_001200000000.bin WD_SET=97 \
//     WD_FROM=1207780000 WD_TO=1207820000 ./build/linux_walkerdata_probe.out

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "sim/rtl/rtl_model.h"
#include "verilated_save.h"

int main(int argc, char **argv) {
  const char *ckpt = getenv("CKPT_RESTORE");
  if (!ckpt) { std::fprintf(stderr, "set CKPT_RESTORE\n"); return 1; }
  const uint32_t SET = getenv("WD_SET") ? (uint32_t)atoi(getenv("WD_SET")) : 97;
  const uint64_t FROM = getenv("WD_FROM")
      ? strtoull(getenv("WD_FROM"), nullptr, 0) : 1207780000ULL;
  const uint64_t TO = getenv("WD_TO")
      ? strtoull(getenv("WD_TO"), nullptr, 0) : 1207820000ULL;

  uint64_t cyc = 0, msip[4] = {}, last_uart = 0, uart_bytes = 0;
  simulator bench;
  bench.init_no_image();
  Vsystem *tb = bench.raw();
  {
    VerilatedRestore rs;
    rs.open(ckpt);
    if (!rs.isOpen()) { std::fprintf(stderr, "cannot open %s\n", ckpt); return 1; }
    rs >> cyc; rs >> msip[0]; rs >> msip[1]; rs >> msip[2]; rs >> msip[3];
    rs >> last_uart; rs >> uart_bytes; rs >> *tb; rs.close();
  }

  std::printf("restored %s at %" PRIu64 "\nwatching hart0 set %u, window %"
              PRIu64 " .. %" PRIu64 "\n", ckpt, cyc, SET, FROM, TO);
  std::printf("states: 0=idle 1=read 2=capture 3=emit 4=drain\n\n");
  std::fflush(stdout);

#define CL(s) tb->system__DOT__chiron__DOT__core0__DOT__memAccess__DOT__cacheLookup__DOT__##s

  int prev_state = -1;
  for (;;) {
    tb->eval();

    if (cyc >= FROM && cyc < TO) {
      const uint32_t st  = CL(flushState);
      const uint32_t fs  = CL(flushSet);
      // Narrate the walker's pass over the watched set, plus every state change
      // while near it, so the capture cycle and its two neighbours are visible.
      const bool near = (fs == SET);
      if (near || (st != (uint32_t)prev_state && prev_state >= 0 && near)) {
        std::printf(
          "%12" PRIu64 " st=%u set=%3u way=%u poison=%u | "
          "dat0: rdAddr=%3u pipe=%3u wrEna=%u wrAddr=%3u fwd=%u "
          "rdData0=%08x wrDataReg0=%08x mem[%u][0]=%08x | "
          "tag: rdAddr=%3u fwd=%u rdData0=%08x mem[%u][0]=%08x\n",
          cyc, st, fs, (unsigned)CL(flushWay), (unsigned)CL(flushSetPoison),
          (unsigned)CL(dataBRAM_0_rdAddr),
          (unsigned)CL(dataBRAM_0__DOT__mem_memData_addr_pipe_0),
          (unsigned)CL(dataBRAM_0_wrEna), (unsigned)CL(dataBRAM_0_wrAddr),
          (unsigned)CL(dataBRAM_0__DOT__doForwardReg),
          CL(dataBRAM_0_rdData)[0], CL(dataBRAM_0__DOT__wrDataReg)[0],
          SET, CL(dataBRAM_0__DOT__mem)[SET][0],
          (unsigned)CL(tagBRAM_rdAddr),
          (unsigned)CL(tagBRAM__DOT__doForwardReg),
          CL(tagBRAM_rdData)[0], SET, CL(tagBRAM__DOT__mem)[SET][0]);
      }
      prev_state = (int)st;

      if (CL(walkerWriteBackBuffer_valid)) {
        static uint64_t last_wwb = ~0ULL;
        const uint64_t a = CL(walkerWriteBackBuffer_address) & ~63ULL;
        if (a != last_wwb) {
          std::printf("%12" PRIu64 "   >>> walkerWB posts %08" PRIx64
                      " word0=%08x\n", cyc, a,
                      ((const uint32_t *)CL(walkerWriteBackBuffer_data))[0]);
          last_wwb = a;
        }
      }
    }

    if (cyc >= TO) break;
    tb->clock = 1; tb->eval();
    tb->clock = 0; tb->eval();
    cyc++;
    if (cyc % 1000000 == 0) {
      std::fprintf(stderr, "  [cyc %" PRIu64 "]\n", cyc);
      std::fflush(stderr);
    }
  }
  std::printf("\ndone at %" PRIu64 "\n", cyc);
  return 0;
}
