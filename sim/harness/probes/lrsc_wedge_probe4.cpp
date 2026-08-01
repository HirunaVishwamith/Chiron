// lrsc_wedge_probe4.cpp — cycle-accurate trace of ONE core's cacheLookup pass
// stage during the mt-lrscirq livelock: every cycle in the window where the
// core's readBuffer holds a valid op, print the op (decoded LR/SC/AMO/LD/ST/
// COH + read/write pass), address, requestType, response, plus the tag-BRAM
// entry for the owner line's set (valid/shared/dirty per way) and the
// reservation/guard state. Shows exactly why the SC read pass fails.
//
// Build:  make build/lrsc_wedge_probe4.out
// Run  :  LRSCP_FROM=.. LRSCP_END=.. [LRSCP_CORE=3] \
//         build/lrsc_wedge_probe4.out bins/mt-lrscirq-short-q4.bin sim/data/qemu.dtb sim/data/boot.bin
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sim/rtl/rtl_model.h"

#define CL3(sig) tb_->system__DOT__chiron__DOT__core3__DOT__memAccess__DOT__cacheLookup__DOT__##sig
#define CL1(sig) tb_->system__DOT__chiron__DOT__core1__DOT__memAccess__DOT__cacheLookup__DOT__##sig

static const char *opname(uint32_t insn, int wd) {
  uint32_t opc = insn & 0x7f;
  if (opc == 0x2f) {
    uint32_t f5 = insn >> 27;
    if (f5 == 0x02) return wd ? "LR.w " : "LR.r ";
    if (f5 == 0x03) return wd ? "SC.w " : "SC.r ";
    return wd ? "AMO.w" : "AMO.r";
  }
  if (opc == 0x03) return "LD   ";
  if (opc == 0x23) return "ST   ";
  return "??   ";
}

int main(int argc, char **argv) {
  const char *image   = (argc > 1) ? argv[1] : "bins/mt-lrscirq-short-q4.bin";
  const char *dtb     = (argc > 2) ? argv[2] : "sim/data/qemu.dtb";
  const char *bootrom = (argc > 3) ? argv[3] : "sim/data/boot.bin";

  simulator bench;
  bench.init(image, dtb, bootrom);
  Vsystem *tb_ = bench.raw();

  auto env64 = [](const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    return v ? strtoull(v, nullptr, 0) : dflt;
  };
  const uint64_t END       = env64("LRSCP_END",  10003000ULL);
  const uint64_t SNAP_FROM = env64("LRSCP_FROM", 10000000ULL);

  // Tag entry decode: mem[set] is 3x32b words holding 4 ways x 23 bits:
  //   way{PLRU(22),share(21),dirty(20),valid(19),tag[18:0]} (tag=addr[31:13]).
  //   Set of 0x...d80 = (0xd80>>6)&0x7f.
  const int SET = (0xd80 >> 6) & 0x7f;
  auto tagbits = [&](int way) {
    uint32_t *w = &CL3(tagBRAM__DOT__mem)[SET][0];
    unsigned __int128 e = ((unsigned __int128)w[2] << 64) |
                          ((unsigned __int128)w[1] << 32) | w[0];
    return (uint64_t)((e >> (23 * way)) & 0x7fffff);
  };

  uint64_t cyc = 0;
  while (cyc < END) {
    tb_->eval();
    tb_->clock = 1; tb_->eval();
    tb_->clock = 0; tb_->eval();
    ++cyc;
    if (cyc < SNAP_FROM) continue;

    if (CL3(readBuffer_valid)) {
      uint32_t insn = (uint32_t)CL3(readBuffer_core_instruction);
      int wd = (int)CL3(readBuffer_writeData_valid);
      char ways[64]; int off = 0;
      for (int wy = 0; wy < 4; wy++) {
        uint64_t t = tagbits(wy);
        off += snprintf(ways + off, sizeof(ways) - off, "S%dD%dV%d.%03llx ",
                        (int)((t >> 21) & 1), (int)((t >> 20) & 1),
                        (int)((t >> 19) & 1), (unsigned long long)(t & 0x7ff));
      }
      printf("%8llu c3 %s a=%05llx rt=%d rsp=%d bv=%d bm=%02x | rv=%d g=%3d b=%d | %s| wr=%llx\n",
             (unsigned long long)cyc, opname(insn, wd),
             (unsigned long long)(CL3(readBuffer_address) & 0xfffff),
             (int)CL3(requestType), (int)CL3(readBuffer_cacheLine_response),
             (int)CL3(readBuffer_branch_valid),
             (int)CL3(readBuffer_branch_mask),
             (int)CL3(reservationRegister_reserved),
             (int)CL3(reservationGuardCnt), (int)CL3(reservationGuardBlocked),
             ways,
             (unsigned long long)(CL3(readBuffer_writeData_data)));
    }
  }
  printf("done cyc=%llu\n", (unsigned long long)cyc);
  return 0;
}
