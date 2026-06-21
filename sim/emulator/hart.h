#pragma once

#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <sys/time.h>
#include <fcntl.h>
#include <iostream>
#include <vector>

#include "constants.h"

using namespace std;

#include "terminal.h"
/**
 * Main hart class, the following functions are defined.
 *
 * @fn hart_step() - Steps through one architectural change of pc of 
 * an independent hart, which includes executing one legal instruction, moving to
 * exception/interrupt handler after a synchronous/asynchronous
 * exception
 * @fn hart_init() - Initializes a hart, i.e.
 *  1. Setting the pc to the first instruction to be executed
 *  2. Setting gprs and csrs
 *  3. Initializes the memory with the image of the program
 * @fn hart_set_interrupts() - Looks for interrupts. If any are set,
 * then in the next step() the pc moves to the exception handler
 * @fn hart_csr_set() - Performs settings CSRs such as mcycle
 * @fn hart_fetch_instruction() - Performs a memory read for an
 * instruction
 *
 * The above functions may be overwritten when adopting the same
 * code for co-simulation and emulation with hardware simulation
 * of a RISC-V core
 * e.g.: To match peripheral accesses between the emulator and
 * simulator are synchrocous, memory_read() will change to provide
 * this functionality.
 */

class hart
{
private:
  /**
   * Initiate all the variables needed to define the state of the
   * emulator.
   * e.g.: gprs, csrs, memory, etc...
   */

  bool csr_read_success = false;
  plevel_t cp = (plevel_t)MMODE;
  bool LD_ADDR_MISSALIG = false;    // load address misalignmen
  bool STORE_ADDR_MISSALIG = false; // store/amo address misali
  bool ILL_INS = false;             // illegal instruction
  bool EBREAK = false;              // break point
  bool INS_ADDR_MISSALIG = false;
  bool INS_ACC_FAULT = false; // instruction access fault
  bool LD_ACC_FAULT = false;  // load access fault

  uint64_t DRAM_BASE = 0x80000000; /// **** this has to change inorder to write to seperate memory location

  enum opcode_t opcode;
  uint64_t rd = 0;
  uint64_t func3 = 0;
  uint64_t rs1 = 0;
  uint64_t rs2 = 0;
  uint64_t func7 = 0;
  uint64_t imm11_0 = 0;
  uint64_t imm31_12 = 0;
  uint64_t imm_j = 0;
  uint64_t imm_b = 0;
  uint64_t imm_s = 0;
  uint32_t imm = 0;
  uint64_t amo_op = 0;

  bool amo_reserve_valid = false;
  bool amo_reserve_valid64 = false;
  uint64_t amo_reserve_addr = 0;
  uint64_t amo_reserve_addr64 = 0;

  uint64_t wb_data = 0;

  uint64_t load_addr = 0;
  uint64_t load_data = 0;
  bool ls_success = false;

  uint64_t store_addr = 0;
  uint64_t store_data = 0;
  uint64_t val = 0;

  bool branch = false;

  uint64_t csr_data = 0;
  bool csr_bool = false;

  uint64_t itr = 0;

  uint64_t cycle_count = 0;

  __uint128_t mult_temp = 0;

  uint64_t ret_data = 0;

  uint64_t PC;
  uint64_t PC_phy;
  uint64_t instruction;

  uint64_t misa = 0b100000001000100000001 | (0b1llu << 63);
  uint64_t mscratch = 0;
  uint64_t medeleg = 0;
  uint64_t mideleg = 0;
  uint64_t mepc = 0;
  uint64_t uepc = 0;
  uint64_t mtval = 0;
  uint64_t mcounteren = 0;
  uint64_t scounteren = 0;
  uint64_t pmpcfg0 = 0;
  uint64_t pmpaddr0 = 0;
  uint64_t mhartid;
  uint64_t mvendorid = 0;
  uint64_t marchid = 0;
  uint64_t mimpid = 0;

  struct timeval tv;
  uint64_t time_in_micros;

  uint64_t &mtime;
  uint64_t &mtimecmp;


  // ── CSR file: control/status registers (csr_read / csr_write) ──────────
#include "hart_csr.inc"

  // ── Trap entry: exception + interrupt vectoring ───────────────────────
#include "hart_trap.inc"

  // ── ALU helpers: sign-extend, high-multiply, integer divide ───────────
#include "hart_alu.inc"

  // ── Memory access: load / store width + alignment ─────────────────────
#include "hart_memory.inc"

public:

  //Constructor
  hart(vector<uint64_t> &memory):mtime(memory.at(MTIME_ADDR / 8)),mtimecmp(memory.at(MTIMECMP_ADDR / 8))
  {
      // memory.at(MTIME_ADDR / 8) = 0;
      // memory.at(MTIMECMP_ADDR / 8) = -1;

      // PC = DRAM_BASE;
      // PC_phy = 0;
      // instruction = 0;
  }
  // uint64_t get_semphore_status() { return (((amo_reserve_addr64&0x00000000FFFFFFF8UL) | amo_reserve_valid64) << 32) | ((amo_reserve_addr&0x00000000FFFFFFFCUL) | amo_reserve_valid); }
  uint64_t get_mstatus() { return mstatus.read_reg(); }

  vector<uint64_t> reg_file = vector<uint64_t>(32);          // register file

  void show_state() {
    printf("pc: %016lx mstatus: %016lx mie: %016lx mcause: %016lx mepc: %016lx rx_ready: %d\n\
    [%lu] x00: %016lx x01: %016lx x02: %016lx x03: %016lx x04: %016lx x05: %016lx x06: %016lx x07: %016lx\n\
    [%lu] x08: %016lx x09: %016lx x10: %016lx x11: %016lx x12: %016lx x13: %016lx x14: %016lx x15: %016lx\n\
    [%lu] x16: %016lx x17: %016lx x18: %016lx x19: %016lx x20: %016lx x21: %016lx x22: %016lx x23: %016lx\n\
    [%lu] x24: %016lx x25: %016lx x26: %016lx x27: %016lx x28: %016lx x29: %016lx x30: %016lx x31: %016lx\n",
    PC, mstatus.read_reg(), mie.read_reg(), mcause.read_reg(), mepc, 1,
    mhartid, reg_file[0], reg_file[1], reg_file[2], reg_file[3], reg_file[4], reg_file[5], reg_file[6], reg_file[7], 
    mhartid, reg_file[8], reg_file[9], reg_file[10], reg_file[11], reg_file[12], reg_file[13], reg_file[14], reg_file[15],
    mhartid, reg_file[16], reg_file[17], reg_file[18], reg_file[19], reg_file[20], reg_file[21], reg_file[22], reg_file[23],
    mhartid, reg_file[24], reg_file[25], reg_file[26], reg_file[27], reg_file[28], reg_file[29], reg_file[30], reg_file[31]);
  }

  __uint64_t get_pc() { return PC; }

  //__uint64_t fetch_long(__uint64_t offset) { return memory.at(offset / 8); }

  int is_peripheral_read(vector<uint64_t> &memory) {
     __uint32_t instruction = hart_fetch_instruction(PC,memory);
     __uint64_t load_addr = reg_file[(instruction >> 15) & 0x1f] + (((__uint64_t)((__int32_t) instruction)) >> 20);
     if ((instruction & 0x7f) != 0b0000011) { return 0; } 
     if ((load_addr >= DRAM_BASE) & (load_addr <= (DRAM_BASE + 0x9000000))) { return 0; } else { return 1; }
   }

  uint32_t get_instruction(vector<uint64_t> &memory) {
    return hart_fetch_instruction(PC,memory);
   }

  void set_register_with_value(__uint8_t rd,__uint64_t value) {
    reg_file[rd] = value;
  }

  /**
   * Initializes a hart, i.e.
   * 1. Setting the pc to the first instruction to be executed
   * 2. Setting gprs and csrs
   * 3. Initializes the memory with the image of the program
   * @param image_name filename of the kernel image to be loaded to
   *  emulator memory
   * return 0 - to signal an error
   */
  void hart_init(vector<uint64_t> &memory,uint8_t hid)
  {
    memory.at(MTIME_ADDR / 8) = 0;
    memory.at(MTIMECMP_ADDR / 8) = -1;

    PC = DRAM_BASE;
    PC_phy = 0;
    instruction = 0;
    mhartid = static_cast<uint64_t>(hid);

  }


  /**
   * Perform a memory read for an instruction
   * @param PC pc of the instruction
   */
  __uint64_t hart_fetch_instruction(__uint64_t PC,vector<uint64_t> &memory)
  {
    PC_phy = PC - DRAM_BASE;

    if (PC % 4 == 0)
    {
      instruction = getINST(PC_phy / 4, &memory);
      return instruction;
    }
    else
    {
      INS_ADDR_MISSALIG = true;
      PC -= PC % 4;
    }
    return 0UL;
  }

  #ifndef LOCKSTEP
  /**
   * Sets up interrupts to execute instructions next
   * i.e.: sets up *TIP, *SIP, *EIP
   */
  void hart_set_interrupts()
  {
    mip.MTIP = (mtime >= mtimecmp);

    if (signed_value(PC) < 0)
    {
      INS_ACC_FAULT = true;
    }

    if (LD_ACC_FAULT)
    {
      LD_ACC_FAULT = false;
      PC = excep_function(PC, CAUSE_LOAD_ACCESS, CAUSE_LOAD_ACCESS, CAUSE_LOAD_ACCESS, cp);
    }
    else if (mie.MEIE && mip.MEIP)
    {
      PC = interrupt_function(PC, CAUSE_MACHINE_EXT_INT, cp);
    }
    else if (mie.MSIE & mip.MSIP)
    {
      PC = interrupt_function(PC, CAUSE_MACHINE_SOFT_INT, cp);
    }
    else if (mie.MTIE && mip.MTIP)
    {
      PC = interrupt_function(PC, CAUSE_MACHINE_TIMER_INT, cp);
    }
    else if (mie.UEIE & mip.UEIP)
    {
      PC = interrupt_function(PC, CAUSE_USER_EXT_INT, cp);
    }
    else if (mie.UTIE & mip.UTIP)
    {
      PC = interrupt_function(PC, CAUSE_USER_TIMER_INT, cp);
    }
    else if (mie.USIE & mip.USIP)
    {
      PC = interrupt_function(PC, CAUSE_USER_SOFT_INT, cp);
    }
    else if (INS_ACC_FAULT)
    {
      INS_ACC_FAULT = false;
      PC = excep_function(PC, CAUSE_FETCH_ACCESS, CAUSE_FETCH_ACCESS, CAUSE_FETCH_ACCESS, cp);
    }
    else if (ILL_INS)
    {
      ILL_INS = false;
      PC = excep_function(PC, CAUSE_ILLEGAL_INSTRUCTION, CAUSE_ILLEGAL_INSTRUCTION, CAUSE_ILLEGAL_INSTRUCTION, cp);
    }
    else if (INS_ADDR_MISSALIG)
    {
      INS_ADDR_MISSALIG = false;
      PC = excep_function(PC, CAUSE_MISALIGNED_FETCH, CAUSE_MISALIGNED_FETCH, CAUSE_MISALIGNED_FETCH, cp);
    }
    else if (EBREAK)
    {
      EBREAK = false;
      PC = excep_function(PC,CAUSE_BREAKPOINT,CAUSE_BREAKPOINT,CAUSE_BREAKPOINT,cp);
    }
    else if (LD_ADDR_MISSALIG)
    {
      LD_ADDR_MISSALIG = false;
      PC = excep_function(PC, CAUSE_MISALIGNED_LOAD, CAUSE_MISALIGNED_LOAD, CAUSE_MISALIGNED_LOAD, cp);
    }
    else if (STORE_ADDR_MISSALIG)
    {
      STORE_ADDR_MISSALIG = false;
      PC = excep_function(PC, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, cp);
    }
  }
  #else
  // this sets up timer interrupt
  // returns a positive integer [error code] if its not possible to set up interrupt
  int hart_set_interrupts(vector<uint64_t> &memory) {
    // emulator-simulator state semantic mis matches
    // look at the pc definitions when comparing state
    hart_step(memory);

    // mip.MTIP = 1; //(mtime >= mtimecmp);
    if (!mie.MTIE || !mstatus.mie) { return 1; }

    PC = interrupt_function(PC, CAUSE_MACHINE_TIMER_INT, cp);
    return 0;
  }

  #endif

  /**
   * Steps through one architectural change of pc of a hart,
   * which includes executing one legal instruction, moving to
   * exception/interrupt handler after a synchronous/asynchronous
   * exception
   */

  // ── Instruction step: fetch → decode → execute → writeback ────────────
#include "hart_execute.inc"
};