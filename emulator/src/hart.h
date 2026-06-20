/*
This heder file represent a one core(hart) of the processor.
*/

/* HART INCLUDE HEADER FILES */

#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string>
#include <fstream>
#include <stdio.h>
#include <bits/stdc++.h>
#include <sys/time.h>
#include <fcntl.h>
#include <iostream>
#include <vector>

#include "constants.h"

using namespace std;

// detecting a key hit
int kbhit()
{
  int byteswaiting;
  ioctl(0, FIONREAD, &byteswaiting);
  return byteswaiting > 0;
}

// disables echo for terminal
void enable_raw_mode()
{
  termios term;
  tcgetattr(0, &term);
  term.c_lflag &= ~(ICANON | ECHO); // Disable echo as well
  tcsetattr(0, TCSANOW, &term);
}

// return terminal back to original state before termination of the program
void disable_raw_mode()
{
  termios term;
  tcgetattr(0, &term);
  term.c_lflag |= ICANON | ECHO;
  tcsetattr(0, TCSANOW, &term);
}


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

  string line;
  uint64_t temp;
  uint64_t i = 0;

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

  
  // vector<uint64_t> reg_file = vector<uint64_t>(32);          // register file

  // *** edit the init fuction such that it sperate from emulators functionality i.e two times initializing mem
  // *** remove meomory and make it shared

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

  struct satp_t
  {
    uint64_t PPN;
    uint16_t ASID;
    uint8_t MODE = 0;
    satp_t()
    {
      PPN = 0;
      ASID = 0;
      MODE = 0;
    }
    uint64_t read_reg()
    {
      return (((uint64_t)(MODE & 0xF) << 60) + ((uint64_t)ASID << 44) + (PPN & 0xFFFFFFFFFFFull));
    }

    void write_reg(const uint64_t &val)
    {
      PPN = val & 0xFFFFFFFFFFFull;
      ASID = 0; //(val>>44) & 0xFFFF ;
      MODE = (val >> 60) & 0xF;
      if (MODE != 8)
      {
        MODE = 0;
      }
    }
  } satp; //

  struct mstatus_t
  {
    uint8_t uie, sie, mie, upie, spie, mpie, spp, mpp, fs, xs, mprv, sum, mxr, tvm, tw, tsr, uxl, sxl, sd;
    mstatus_t()
    {
      uie = 0;
      sie = 0;
      mie = 0;
      upie = 0;
      spie = 0;
      mpie = 0;
      spp = 0;
      mpp = 0b0;
      fs = 0;
      xs = 0;
      mprv = 0;
      sum = 0;
      mxr = 0;
      tvm = 0;
      tw = 0;
      tsr = 0;
      uxl = 2;
      sxl = 2;
      sd = 0;
    }
    uint64_t read_reg()
    {
      return (((uint64_t)sd << 63) + ((uint64_t)sxl << 34) + ((uint64_t)uxl << 32) + (tsr << 22) + (tw << 21) + (tvm << 20) + (mxr << 19) + (sum << 18) + (mprv << 17) + (xs << 15) + (fs << 13) + (mpp << 11) + (spp << 8) + (mpie << 7) + (spie << 5) + (upie << 4) + (mie << 3) + (sie << 1) + uie);
    }

    void write_reg(const uint64_t &val)
    {
      uie = (val & 0b1);
      sie = ((val >> 1) & 0b1);
      mie = ((val >> 3) & 0b1);
      upie = ((val >> 4) & 0b1);
      spie = ((val >> 5) & 0b1);
      mpie = ((val >> 7) & 0b1);
      spp = ((val >> 8) & 0b1);
      mpp = ((val >> 11) & 0b11); // 0b11; removed hard wire to 11
      fs = ((val >> 13) & 0b11);
      xs = ((val >> 15) & 0b11);
      mprv = ((val >> 17) & 0b1);
      sum = ((val >> 18) & 0b1);
      mxr = ((val >> 19) & 0b1);
      tvm = ((val >> 20) & 0b1);
      tw = ((val >> 21) & 0b1);
      tsr = ((val >> 22) & 0b1); /*uxl= ((val>>32)& 0b11); read only*/
      //sxl = ((val >> 34) & 0b11);
      sd = ((val >> 63) & 0b1);
    }
  } mstatus; //

  struct ustatus_t
  {
    // uint8_t uie, sie, upie, spie, spp, fs, xs, sum, mxr, uxl, sd;
    uint8_t uie = 0;
    uint8_t upie = 0;
    uint8_t fs = 0;
    uint8_t xs = 0;
    uint8_t sum = 0;
    uint8_t mxr = 0;
    uint8_t uxl = 2;
    uint8_t sd = 0;
    ustatus_t()
    {
    }
    uint64_t read_reg()
    {
      return (((uint64_t)sd << 63) + ((uint64_t)uxl << 32) + (mxr << 19) + (sum << 18) + (xs << 15) + (fs << 13) + (upie << 4) + uie);
    }
    void write_reg(const uint64_t &val)
    {
      uie = (val & 0b1);
      upie = ((val >> 5) & 0b1);
      fs = ((val >> 13) & 0b11);
      xs = ((val >> 15) & 0b11);
      sum = ((val >> 18) & 0b1);
      mxr = ((val >> 19) & 0b1);
      uxl = ((val >> 32) & 0b11);
      sd = ((val >> 63) & 0b1);
    }
  } ustatus;

  struct mie_t
  {
    uint8_t MEIE;
    uint8_t SEIE;
    uint8_t UEIE;
    uint8_t MTIE;
    uint8_t STIE;
    uint8_t UTIE;
    uint8_t MSIE;
    uint8_t SSIE;
    uint8_t USIE;
    mie_t()
    {
      MEIE = 0;
      SEIE = 0;
      UEIE = 0;
      MTIE = 0;
      STIE = 0;
      UTIE = 0;
      MSIE = 0;
      SSIE = 0;
      USIE = 0;
    }
    uint64_t read_reg()
    {
      return (((MEIE & 0b1) << 11) + ((SEIE & 0b1) << 9) + ((UEIE & 0b1) << 8) + ((MTIE & 0b1) << 7) + ((STIE & 0b1) << 5) + ((UTIE & 0b1) << 4) + ((MSIE & 0b1) << 3) + ((SSIE & 0b1) << 1) + (USIE & 0b1));
    }
    void write_reg(const uint64_t &val)
    {
      MEIE = (val >> 11) & 0b1;
      SEIE = (val >> 9) & 0b1;
      UEIE = (val >> 8) & 0b1;
      MTIE = (val >> 7) & 0b1;
      STIE = (val >> 5) & 0b1;
      UTIE = (val >> 4) & 0b1;
      MSIE = (val >> 3) & 0b1;
      SSIE = (val >> 1) & 0b1;
      USIE = val & 0b1;
    }
  } mie; //

  struct mip_t
  {
    uint8_t MEIP;
    uint8_t SEIP;
    uint8_t UEIP;
    uint8_t MTIP;
    uint8_t STIP;
    uint8_t UTIP;
    uint8_t MSIP;
    uint8_t SSIP;
    uint8_t USIP;
    mip_t()
    {
      MEIP = 0;
      SEIP = 0;
      UEIP = 0;
      MTIP = 0;
      STIP = 0;
      UTIP = 0;
      MSIP = 0;
      SSIP = 0;
      USIP = 0;
    }
    uint64_t read_reg()
    {
      return (((MEIP & 0b1) << 11) + ((SEIP & 0b1) << 9) + ((UEIP & 0b1) << 8) + ((MTIP & 0b1) << 7) + ((STIP & 0b1) << 5) + ((UTIP & 0b1) << 4) + ((MSIP & 0b1) << 3) + ((SSIP & 0b1) << 1) + (USIP & 0b1));
    }

    void write_reg(const uint64_t &val)
    {
      MEIP = (val >> 11) & 0b1;
      SEIP = (val >> 9) & 0b1;
      UEIP = (val >> 8) & 0b1;
      MTIP = (val >> 7) & 0b1;
      STIP = (val >> 5) & 0b1;
      UTIP = (val >> 4) & 0b1;
      MSIP = (val >> 3) & 0b1;
      SSIP = (val >> 1) & 0b1;
      USIP = val & 0b1;
    }
  } mip; //

  struct mtvec_t
  {
    uint8_t mode;
    uint64_t base;
    mtvec_t()
    {
      mode = 0;
      base = 0;
    }
    uint64_t read_reg()
    {
      return ((mode & 0b11) + (base & (MASK64 - 0b11)));
    }

    void write_reg(const uint64_t &val)
    {
      mode = val & 0b11;
      base = (val & (MASK64 - 0b11));
    }
  } mtvec; //

  struct utvec_t
  {
    uint8_t mode;
    uint64_t base;
    utvec_t()
    {
      mode = 0;
      base = 0;
    }
    uint64_t read_reg()
    {
      return ((mode & 0b11) + (base & (MASK64 - 0b11)));
    }
    void write_reg(const uint64_t &val)
    {
      mode = val & 0b11;
      base = (val & (MASK64 - 0b11));
    }
  } utvec;

  struct mcause_t
  {
    uint8_t interrupt;
    uint64_t ecode;
    mcause_t()
    {
      interrupt = 0;
      ecode = 0;
    }
    uint64_t read_reg()
    {
      // cout << "mcause: " << dec << (((1llu<<63)-1) & ecode)+((uint64_t)interrupt<<63);
      return ((((1llu << 63) - 1) & ecode) + ((uint64_t)interrupt << 63));
    }

    void write_reg(const uint64_t &val)
    {
      ecode = val & ((1llu << 63) - 1);
      interrupt = (val >> 63) & 0b1;
    }
  } mcause; //

  struct ucause_t
  {
    uint8_t interrupt;
    uint64_t ecode;
    ucause_t()
    {
      interrupt = 0;
      ecode = 0;
    }
    uint64_t read_reg()
    {
      return ((((1llu << 63) - 1) & ecode) + ((uint64_t)interrupt << 63));
    }

    void write_reg(const uint64_t &val)
    {
      ecode = val & ((1llu << 63) - 1);
      interrupt = (val >> 63) & 0b1;
    }
  } ucause;

  uint64_t csr_read(const uint64_t &csr_addr)
  {
    csr_read_success = true;
    switch (csr_addr)
    {
    case MSTATUS:
      if (cp == MMODE)
      {
        csr_read_success = true;
        return mstatus.read_reg();
      }
      else
      {
        csr_read_success = false;
        return 1;
      }
    case SATP:
      return satp.read_reg();
    case MIE:
      return mie.read_reg();
    case MIP:
      return mip.read_reg();
    case MTVEC:
      return mtvec.read_reg();
    case MEPC:
      return mepc;
    case MCAUSE:
      return mcause.read_reg();
    case MTVAL:
      return mtval;
    case PMPCFG0:
      return pmpcfg0;
    case PMPADDR0:
      return pmpaddr0;
    case MHARTID:
      return mhartid;
    case MVENDORID:
      return mvendorid;
      break;
    case MARCHID:
      return marchid;
      break;
    case MIMPID:
      return mimpid;
      break;
    case MSCRATCH:
      return mscratch;
    case MISA:
      return misa;
    case SCOUNTEREN:
      return scounteren;
    case MCOUNTEREN:
      return mcounteren;
    default:
      csr_read_success = false;
      return 1;
    }
  }

  bool csr_write(const uint64_t &csr_addr, const uint64_t &val)
  {
    switch (csr_addr)
    {
    case MSTATUS:
      mstatus.write_reg(val);
      return true;
    case SATP:
      satp.write_reg(val);
      return true;
    case MIE:
      mie.write_reg(val);
      return true;
    case MIP:
      mip.write_reg(val);
      return true;
    case MTVEC:
      mtvec.write_reg(val);
      return true;
    case MEPC:
      mepc = val;
      return true;
    case MCAUSE:
      mcause.write_reg(val);
      return true;
    case MTVAL:
      mtval = val;
      return true;
    case PMPCFG0:
      pmpcfg0 = val;
      return true;
    case PMPADDR0:
      pmpaddr0 = val;
      return true;
    case MHARTID:
      mhartid = val;
      return true;
    case MVENDORID:
      mvendorid = val;
      return true;
    case MARCHID:
      marchid = val;
      return true;
    case MIMPID:
      mimpid = val;
      return true;
    case MSCRATCH:
      mscratch = val;
      return true;
    case MISA:
      misa = val;
      return true;
    case SCOUNTEREN:
      scounteren = val;
      return true;
    case MCOUNTEREN:
      mcounteren = val;
      return true;
    default:
      return false;
    }
  }

  uint64_t excep_function(const uint64_t &PC, const uint64_t &mecode, const uint64_t &secode, const uint64_t &uecode, const plevel_t &current_privilage)
  {
    uint64_t ecode = 0;
    uint64_t new_PC = 0;

    if (current_privilage == UMODE)
    {
      ecode = uecode;
    }
    else if (current_privilage == MMODE)
    {
      ecode = mecode;
    }

    mstatus.mpp = (uint64_t)cp;
    cp = MMODE;
    mstatus.mpie = mstatus.mie;
    mstatus.mie = 0;
    mcause.interrupt = 0;
    mcause.ecode = ecode;
    mepc = PC - 4;
    new_PC = mtvec.base;

    return new_PC;
  }

  uint64_t interrupt_function(const uint64_t &PC, const uint64_t &mecode, const plevel_t &current_privilage)
  {
    uint64_t ecode = 0;
    uint64_t new_PC = 0;

    ecode = mecode;

    if (mstatus.mie == 1)
    {
      mstatus.mpp = (uint64_t)cp;
      cp = MMODE;
      mstatus.mpie = mstatus.mie;
      mstatus.mie = 0;
      mcause.interrupt = 1;
      mcause.ecode = ecode;
      mepc = PC;

      if (mtvec.mode == 0b0)
      {
        new_PC = mtvec.base;
      }
      else if (mtvec.mode == 0b1)
      {
        new_PC = mtvec.base + 4 * ecode;
      }
    }
    else
    {
      // cout << "Unrecognized mode for interrupt_function" << endl;
      return PC;
    }
    return new_PC;
  }

  uint64_t getINST(const uint64_t &PC, const vector<uint64_t> *memory)
  {
    // cout << "This is here" << endl;
    if (PC % 2 == 0)
      return ((MASK32) & (memory->at(PC / 2)));
    else
      return ((memory->at(PC / 2)) >> 32);
  }

  int64_t signed_value(const uint64_t &x)
  {
    if (((x >> 63) & 0b1) == 1)
      return (x ^ (1llu << 63)) - (1llu << 63);
    else
      return x;
  }

  int32_t signed_value32(const uint64_t &x)
  {
    uint32_t y = x & MASK32;
    if (((y >> 31) & 0b1) == 1)
      return (y ^ (1lu << 31)) - (1lu << 31);
    else
      return y;
  }

  template <class T>
  T sign_extend(const T &x, const int &bits)
  {
    T m = 1;
    m <<= bits - 1;
    return (x ^ m) - m;
  }

  string exec(const char *cmd)
  {
    char buffer[128];
    string result = "";
    FILE *pipe = popen(cmd, "r");
    if (!pipe)
      throw runtime_error("popen() failed!");
    try
    {
      while (fgets(buffer, sizeof buffer, pipe) != NULL)
      {
        result += buffer;
      }
    }
    catch (...)
    {
      pclose(pipe);
      throw;
    }
    pclose(pipe);
    return result;
  }

  int hex_to_dec(const int64_t &hex_num)
  {
    stringstream stream;
    int y;
    stream << hex_num;
    stream >> hex >> y;
    return y;
  }

  void print_reg_file(const vector<uint64_t> &reg_file)
  {
    printf("\n");
    for (int i = 0; i < 32 - 7; i += 8)
    {
      printf("%s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu\n",
             reg_file_names[i].c_str(), signed_value(reg_file[i]),
             reg_file_names[i + 1].c_str(), signed_value(reg_file[i + 1]),
             reg_file_names[i + 2].c_str(), signed_value(reg_file[i + 2]),
             reg_file_names[i + 3].c_str(), signed_value(reg_file[i + 3]),
             reg_file_names[i + 4].c_str(), signed_value(reg_file[i + 4]),
             reg_file_names[i + 5].c_str(), signed_value(reg_file[i + 5]),
             reg_file_names[i + 6].c_str(), signed_value(reg_file[i + 6]),
             reg_file_names[i + 7].c_str(), signed_value(reg_file[i + 7]));
    }
    printf("\n");
  }

  template <class T>
  T divi(T num1, T num2, int s)
  {
    if (num2 == 0)
    {
      switch (s)
      {
      case 0:
        return (T)(-1);
      case 1:
        return (T)MASK64;
      case 2:
        return num1;
      case 3:
        return num1;
      }
    }
    else if (num1 == (-pow(2ull, 63)) && num2 == -1)
    {
      if (s == 0 || s == 2)
      {
        switch (s)
        {
        case 0:
          return (T)(-pow(2ull, 63));
        case 2:
          return 0;
        }
      }
    }
    else
    {
      ldiv_t div_result;
      switch (s)
      {
      case 0:
        div_result = div((int64_t)num1, (int64_t)num2);
        return div_result.quot;
      case 1:
        return num1 / num2;
      case 2:
        div_result = div((int64_t)num1, (int64_t)num2);
        return div_result.rem;
      case 3:
        return num1 % num2;
      default:
        return (T)(-1);
      }
    }
    return (T)(-1);
  }

  template <class T>
  T divi32(T num1, T num2, int s)
  {
    if (num2 == 0)
    {
      switch (s)
      {
      case 0:
        return (T)(-1);
      case 1:
        return (T)MASK32;
      case 2:
        return num1;
      case 3:
        return num1;
      }
    }
    else if (num1 == (-pow(2, 31)) && num2 == -1)
    {
      if (s == 0 || s == 2)
      {
        switch (s)
        {
        case 0:
          return (T)(-pow(2ull, 31));
        case 2:
          return 0;
        }
      }
    }
    else
    {
      div_t div_result;
      switch (s)
      {
      case 0:
        div_result = div((int32_t)num1, (int32_t)num2);
        return div_result.quot;
      case 1:
        return num1 / num2;
      case 2:
        div_result = div((int32_t)num1, (int32_t)num2);
        return div_result.rem;
      case 3:
        return num1 % num2;
      default:
        return (T)(-1);
      }
    }
    return (T)(-1);
  }

  bool load_word(const uint64_t &load_addr, const uint64_t &load_data, uint64_t &wb_data)
  {
    switch (load_addr % 8)
    {
    case 0:
      wb_data = ((load_data >> 0) & 0xFFFFFFFF);
      break;
    case 1:
      wb_data = ((load_data >> 8) & 0xFFFFFFFF);
      break;
    case 2:
      wb_data = ((load_data >> 16) & 0xFFFFFFFF);
      break;
    case 3:
      wb_data = ((load_data >> 24) & 0xFFFFFFFF);
      break;
    case 4:
      wb_data = ((load_data >> 32) & 0xFFFFFFFF);
      break;
    default:
      wb_data = -1;
      return false;
      break;
    }
    return true;
  }

  bool load_halfw(const uint64_t &load_addr, const uint64_t &load_data, uint64_t &wb_data)
  {
    switch (load_addr % 8)
    {
    case 0:
      wb_data = ((load_data >> 0) & 0xFFFF);
      break;
    case 1:
      wb_data = ((load_data >> 8) & 0xFFFF);
      break;
    case 2:
      wb_data = ((load_data >> 16) & 0xFFFF);
      break;
    case 3:
      wb_data = ((load_data >> 24) & 0xFFFF);
      break;
    case 4:
      wb_data = ((load_data >> 32) & 0xFFFF);
      break;
    case 5:
      wb_data = ((load_data >> 40) & 0xFFFF);
      break;
    case 6:
      wb_data = ((load_data >> 48) & 0xFFFF);
      break;
    default:
      wb_data = -1;
      return false;
      break;
    }
    return true;
  }

  bool load_byte(const uint64_t &load_addr, const uint64_t &load_data, uint64_t &wb_data)
  {
    switch (load_addr % 8)
    {
    case 0:
      wb_data = (load_data & 0xFF);
      break;
    case 1:
      wb_data = (((load_data) >> 8) & 0xFF);
      break;
    case 2:
      wb_data = (((load_data) >> 16) & 0xFF);
      break;
    case 3:
      wb_data = (((load_data) >> 24) & 0xFF);
      break;
    case 4:
      wb_data = (((load_data) >> 32) & 0xFF);
      break;
    case 5:
      wb_data = (((load_data) >> 40) & 0xFF);
      break;
    case 6:
      wb_data = (((load_data) >> 48) & 0xFF);
      break;
    case 7:
      wb_data = (((load_data) >> 56) & 0xFF);
      break;
    default:
      wb_data = -1;
      return false;
      break;
    }
    return true;
  }

  bool store_word(const uint64_t &store_addr, const uint64_t &load_data, const uint64_t &value, uint64_t &wb_data)
  {
    switch (store_addr % 8)
    {
    case 0:
      wb_data = (load_data & 0xFFFFFFFF00000000) + ((value & 0xFFFFFFFF) << 0);
      break;
    case 1:
      wb_data = (load_data & 0xFFFFFF00000000FF) + ((value & 0xFFFFFFFF) << 8);
      break;
    case 2:
      wb_data = (load_data & 0xFFFF00000000FFFF) + ((value & 0xFFFFFFFF) << 16);
      break;
    case 3:
      wb_data = (load_data & 0xFF00000000FFFFFF) + ((value & 0xFFFFFFFF) << 24);
      break;
    case 4:
      wb_data = (load_data & 0x00000000FFFFFFFF) + ((value & 0xFFFFFFFF) << 32);
      break;
    default:
      wb_data = -1;
      return false;
      break;
    }
    return true;
  }

  bool store_halfw(const uint64_t &store_addr, const uint64_t &load_data, const uint64_t &value, uint64_t &wb_data)
  {
    switch (store_addr % 8)
    {
    case 0:
      wb_data = (load_data & 0xFFFFFFFFFFFF0000) + ((value & 0xFFFF) << 0);
      break;
    case 1:
      wb_data = (load_data & 0xFFFFFFFFFF0000FF) + ((value & 0xFFFF) << 8);
      break;
    case 2:
      wb_data = (load_data & 0xFFFFFFFF0000FFFF) + ((value & 0xFFFF) << 16);
      break;
    case 3:
      wb_data = (load_data & 0xFFFFFF0000FFFFFF) + ((value & 0xFFFF) << 24);
      break;
    case 4:
      wb_data = (load_data & 0xFFFF0000FFFFFFFF) + ((value & 0xFFFF) << 32);
      break;
    case 5:
      wb_data = (load_data & 0xFF0000FFFFFFFFFF) + ((value & 0xFFFF) << 40);
      break;
    case 6:
      wb_data = (load_data & 0x0000FFFFFFFFFFFF) + ((value & 0xFFFF) << 48);
      break;
    default:
      wb_data = -1;
      return false;
      break;
    }
    return true;
  }

  bool store_byte(const uint64_t &store_addr, const uint64_t &load_data, const uint64_t &value, uint64_t &wb_data)
  {
    switch (store_addr % 8)
    {
    case 0:
      wb_data = (load_data & ((0xFFFFFFFFFFFFFFull) << 8)) + (value & 0xFF);
      break;
    case 1:
      wb_data = (load_data & ((0xFFFFFFFFFFFFull) << 16)) + ((value & 0xFF) << 8) + (load_data & 0xFF);
      break;
    case 2:
      wb_data = (load_data & ((0xFFFFFFFFFFull) << 24)) + ((value & 0xFF) << 16) + (load_data & 0xFFFF);
      break;
    case 3:
      wb_data = (load_data & ((0xFFFFFFFFull) << 32)) + ((value & 0xFF) << 24) + (load_data & 0xFFFFFF);
      break;
    case 4:
      wb_data = (load_data & ((0xFFFFFFull) << 40)) + ((value & 0xFF) << 32) + (load_data & 0xFFFFFFFF);
      break;
    case 5:
      wb_data = (load_data & ((0xFFFFull) << 48)) + ((value & 0xFF) << 40) + (load_data & 0xFFFFFFFFFF);
      break;
    case 6:
      wb_data = (load_data & ((0xFFull) << 56)) + ((value & 0xFF) << 48) + (load_data & 0xFFFFFFFFFFFF);
      break;
    case 7:
      wb_data = ((value & 0xFF) << 56) + (load_data & 0xFFFFFFFFFFFFFF);
      break;
    default:
      wb_data = -1;
      return false;
      break;
    }
    return true;
  }

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
  void hart_step(vector<uint64_t> &memory)
  {
    reg_file[0] = 0;


    gettimeofday(&tv, NULL);
    time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
    mtime = (uint64_t)(time_in_micros * 10);

    instruction = hart_fetch_instruction(PC,memory);

    //--------------Debugging---------------------
    //To check if all tests passed
    if (instruction == 0x00000073 && reg_file[3] == 0x1 && reg_file[17] == 0x5d && reg_file[10] == 0x0) {
      std::cout<<"All tests passed"<<endl;
      disable_raw_mode();
      exit(0);
    }
    if (instruction == 0x00018513) {
      std::cout<<"Tests failed"<<endl;
      disable_raw_mode();
      exit(1);
    }
    //--------------Debugging---------------------

    if (!INS_ADDR_MISSALIG)
    {

      PC = PC + 4;

      opcode = static_cast<opcode_t>((instruction) & 0b1111111);

      rd = ((instruction) >> 7) & 0b11111;
      func3 = ((instruction) >> 12) & 0b111;
      rs1 = ((instruction) >> 15) & 0b11111;
      rs2 = ((instruction) >> 20) & 0b11111;
      func7 = ((instruction) >> 25) & 0b1111111;

      imm11_0 = ((instruction) >> 20) & 0b111111111111;
      imm31_12 = ((instruction) >> 12) & 0xFFFFF; // extract 20 bits

      imm_j = ((((instruction) >> 31) & 0b1) << 20) + ((instruction) & (0b11111111 << 12)) + ((((instruction) >> 20) & 0b1) << 11) + ((((instruction) >> 21) & 0b1111111111) << 1); //((instruction>>31) & 0b1)<<20 + (instruction & (0b11111111<<12)) + ((instruction>>20) & 0b1)<<11 +
      imm_b = ((((instruction) >> 31) & 0b1) << 12) + ((((instruction) >> 7) & 0b1) << 11) + ((((instruction) >> 25) & 0b111111) << 5) + (((instruction) >> 7) & 0b11110);
      imm_s = ((((instruction) >> 25) & 0b1111111) << 5) + (((instruction) >> 7) & 0b11111);
      imm = rd | ((instruction >> (25 - 5)) & 0xfe0);
      imm = (imm << 20) >> 20;

      amo_op = ((instruction) >> 27) & 0b11111;
      reg_file[0] = 0UL;

      switch (opcode)
      {
      case lui:
        wb_data = sign_extend<uint64_t>((instruction & 0xFFFFF000), 32);
        reg_file[rd] = wb_data;
        break;
      case auipc:
        wb_data = (PC - 4) + sign_extend<uint64_t>(((imm31_12) << 12), 32);
        reg_file[rd] = wb_data;
        break;
      case jump:
        wb_data = PC;
        PC = (PC - 4) + sign_extend<uint64_t>(imm_j, 21); // 21 bits sign extend
        if (PC % 4 == 0)
        {
          reg_file[rd] = wb_data; // PC + 4 to rd
        }
        break;
      case jumpr:
        wb_data = PC;
        PC = (reg_file[rs1] + sign_extend<uint64_t>(imm11_0, 12)) & 0xFFFFFFFFFFFFFFFE; // setting LSB to 0 as spec page 20
        if (PC % 4 == 0)
        {
          reg_file[rd] = wb_data; // PC + 4 to rd
        }
        break;
      case cjump:
        switch (func3)
        {
        case 0b000:
          branch = reg_file[rs1] == reg_file[rs2];
          break; // BEQ
        case 0b001:
          branch = (reg_file[rs1] != reg_file[rs2]);
          break; // BNE
        case 0b100:
          branch = (signed_value(reg_file[rs1]) < signed_value(reg_file[rs2]));
          break; // BLT
        case 0b101:
          branch = (signed_value(reg_file[rs1]) >= signed_value(reg_file[rs2]));
          break; // BGE
        case 0b110:
          branch = (reg_file[rs1] < reg_file[rs2]);
          break; // BLTU
        case 0b111:
          branch = (reg_file[rs1] >= reg_file[rs2]);
          break; // BGEU
        default:
          break;
        }
        if (branch)
        {
          PC = PC - 4 + sign_extend<uint64_t>(imm_b, 13);
        }
        break;
      case load:
        load_addr = (reg_file[rs1] + sign_extend<uint64_t>(imm11_0, 12)) & 0x00000000ffffffff;
        if (signed_value(load_addr) < 0)
        {
          LD_ACC_FAULT = true;
        }
        else
        {
          if ((load_addr != FIFO_ADDR_RX) && ((load_addr != FIFO_ADDR_TX)))
          {
            if ((load_addr >= DRAM_BASE) & (load_addr <= (DRAM_BASE + 0x9000000)))
            {
              load_data = memory.at((load_addr - DRAM_BASE) / 8);
            }
            else
            {
              if ((load_addr >= CLINT_BASE) && (load_addr <= CLINT_BASE + CLINT_SIZE))
              {
                uint64_t offset = load_addr - CLINT_BASE;
                switch (offset)
                {
                case 0xbff8: // rtc time
                  load_data = mtime;
                  break;
                case 0x4000: // timecmp
                  load_data = mtimecmp;
                  break;
                default:
                  load_data = 0;
                  break;
                }

                // printf("load_address: %016lx, data: %lu , core: %lu \n", load_addr, load_data , mhartid);
              }
              else
              {
                load_data = 0x1000;
              }
            }
            switch (func3)
            {
            case 0b000:
              if (!load_byte(load_addr, load_data, wb_data))
              {
                LD_ADDR_MISSALIG = true;
              }
              else
              {
                wb_data = sign_extend<uint64_t>(wb_data & (0xFF), 8);
                reg_file[rd] = wb_data;
              }
              break; // LB sign extend  8 bit value
            case 0b001:
              if (!load_halfw(load_addr, load_data, wb_data))
              {
                LD_ADDR_MISSALIG = true;
              }
              else
              {
                wb_data = sign_extend<uint64_t>(wb_data & (0xFFFF), 16);
                reg_file[rd] = wb_data;
              }
              break; // LH sign extend 16 bit value
            case 0b010:
              if (!load_word(load_addr, load_data, wb_data))
              {
                LD_ADDR_MISSALIG = true;
              }
              else
              {
                wb_data = sign_extend<uint64_t>(wb_data & (0xFFFFFFFF), 32);
                reg_file[rd] = wb_data;
              }
              break; // LW sign extend 32 bit value
            case 0b100:
              if (!load_byte(load_addr, load_data, wb_data))
              {
                LD_ADDR_MISSALIG = true;
              }
              else
              {
                wb_data = wb_data & 0xFF;
                reg_file[rd] = wb_data;
              }
              break; // LBU zero extend  8 bit value
            case 0b101:
              if (!load_halfw(load_addr, load_data, wb_data))
              {
                LD_ADDR_MISSALIG = true;
              }
              else
              {
                wb_data = wb_data & 0xFFFF;
                reg_file[rd] = wb_data;
              }
              break; // LHU zero extend 16 bit value
            case 0b110:
              if (!load_word(load_addr, load_data, wb_data))
              {
                LD_ADDR_MISSALIG = true;
              }
              else
              {
                wb_data = wb_data & 0xFFFFFFFF;
                reg_file[rd] = wb_data;
              }
              break; // LWU zero extend 32 bit value
            case 0b011:
              if ((load_addr % 8) == 0)
              {
                wb_data = load_data;
                reg_file[rd] = wb_data;
              }
              else
              {
                LD_ADDR_MISSALIG = true;
              }
              break; // LD
            default:
              break;
            }
             //printf("load_address: %016lx, data: %lu , core: %lu \n", load_addr, load_data , rd);
          }
          else
          {
            if (load_addr == FIFO_ADDR_RX)
            {
              if (kbhit())
              {
                wb_data = 0;
              }
              else
              {
                wb_data = 2;
              }
              reg_file[rd] = wb_data;
            }
            else if (load_addr == FIFO_ADDR_TX)
            {
              #ifndef LOCKSTEP
              wb_data = getchar();
              #endif
              reg_file[rd] = wb_data;
            }
          }
        }
        break;
      case store:
        store_addr = (reg_file[rs1] + sign_extend<uint64_t>(imm_s, 12)) & 0x00000000ffffffff;
        if (store_addr != FIFO_ADDR_TX)
        {
          if ((store_addr >= DRAM_BASE) & (store_addr < (DRAM_BASE + 0x9000000)))
          {
            store_data = memory.at((store_addr - DRAM_BASE) / 8);
            switch (func3)
            { // Setting lower n bits to 0 and adding storing value
            case 0b000:
              val = reg_file[rs2] & 0xFF;
              ls_success = store_byte(store_addr, store_data, val, wb_data);
              break; // SB  setting LSB 8 bit
            case 0b001:
              val = reg_file[rs2] & 0xFFFF;
              ls_success = store_halfw(store_addr, store_data, val, wb_data);
              break; // SH setting LSB 16 bit value
            case 0b010:
              val = reg_file[rs2] & 0xFFFFFFFF;
              ls_success = store_word(store_addr, store_data, val, wb_data);
              break; // SW setting LSB 32 bit value
            case 0b011:
              if ((store_addr % 8) == 0)
              {
                wb_data = reg_file[rs2];
                ls_success = true;
              }
              else
              {
                ls_success = false;
              }
              break; // SD
            default:
              break;
            }
            if (!ls_success)
            {
              PC = excep_function(PC, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, cp);
            }
            else
            {
              memory.at((store_addr - DRAM_BASE) / 8) = wb_data;
              // printf("store_addr: %016lx  wb_data: %016lx ",store_addr,wb_data);
            }
          }
          else
          {
            if ((store_addr >= CLINT_BASE) & (store_addr <= (CLINT_BASE + CLINT_SIZE)))
            {
              uint64_t offset = store_addr - CLINT_BASE;
              switch (offset)
              {
              case 0x4000:
                mtimecmp = reg_file[rs2];
                mip.STIP = 0;
                break;
              default:
                break;
              }

              // printf("store_address: %016lx, data: %lu , core: %lu \n", store_addr, reg_file[rs2], mhartid );
            }
            else
            {
              if (store_addr - DRAM_BASE == FIFO_ADDR_TX)
              {
                cout << (char)reg_file[rs2] << flush;
              }
            }
          }
        }
        else
        {
          cout << (char)reg_file[rs2] << flush;
            //printf("Hi");
            int out_char = reg_file[rs2]; // Extract 8-bit char
            //printf(" %d out_char:\n", out_char);
            //cout << out_char; // Print to terminal
            cout.flush();
        }
        break;
      case iops:
        switch (func3)
        {
        case 0b000:
          wb_data = reg_file[rs1] + signed_value(sign_extend<uint64_t>(imm11_0, 12)); // ADDI
          break;
        case 0b010:
          wb_data = (signed_value(reg_file[rs1]) < signed_value(sign_extend<uint64_t>(imm11_0, 12))) ? 1 : 0; // SLTI
          break;
        case 0b011:
          wb_data = (reg_file[rs1] < sign_extend<uint64_t>(imm11_0, 12)) ? 1 : 0; // SLTIU
          break;
        case 0b111:
          wb_data = reg_file[rs1] & sign_extend<uint64_t>(imm11_0, 12); // ANDI
          break;
        case 0b110:
          wb_data = reg_file[rs1] | sign_extend<uint64_t>(imm11_0, 12); // ORI
          break;
        case 0b100:
          wb_data = reg_file[rs1] ^ sign_extend<uint64_t>(imm11_0, 12); // XORI
          break;
        case 0b001:
          wb_data = reg_file[rs1] << (imm11_0 & 0b111111); // SLLI
          break;
        case 0b101:
          if ((func7 >> 1) == 0b000000)
          {
            wb_data = reg_file[rs1] >> ((imm11_0) & 0b111111); // SRLI
          }
          else if ((func7 >> 1) == 0b010000)
          {
            wb_data = reg_file[rs1];
            for (itr = 0; itr < (imm11_0 & 0b111111); itr++)
            {
              wb_data = (wb_data & (1llu << 63)) | ((wb_data) >> (1)); // SRAI
            }
          }
          break;
        default:
          break;
        }
        reg_file[rd] = wb_data;
        break;
      case iops64:
        switch (func3)
        {
        case 0b000: // ADDIW
          wb_data = sign_extend<uint64_t>(MASK32 & (reg_file[rs1] + sign_extend<uint64_t>(imm11_0, 12)), 32);
          break;
        case 0b001: // SLLIW
          wb_data = sign_extend<uint64_t>(MASK32 & (reg_file[rs1] << (imm11_0 & 0b11111)), 32);
          break;
        case 0b101:
          if ((func7 >> 1) == 0b000000)
          { // SRLIW
            wb_data = sign_extend<uint64_t>(MASK32 & ((reg_file[rs1] & MASK32) >> (imm11_0 & 0b11111)), 32);
          }
          else if ((func7 >> 1) == 0b010000)
          {
            wb_data = reg_file[rs1] & MASK32;
            for (itr = 0; itr < (imm11_0 & 0b11111); itr++)
            {
              wb_data = ((wb_data & ((1llu) << 31)) | ((wb_data) >> (1))); // SRAIW
            }
            wb_data = sign_extend<uint64_t>(MASK32 & wb_data, 32);
          }
          break;
        }
        reg_file[rd] = wb_data;
        break;
      case rops:
        if (func7 == 0b0000000)
        {
          switch (func3)
          {
          case 0b000:
            wb_data = reg_file[rs1] + reg_file[rs2]; // ADD
            break;
          case 0b010:
            wb_data = (signed_value(reg_file[rs1]) < signed_value(reg_file[rs2])) ? 1 : 0; // SLT
            break;
          case 0b011:
            wb_data = (reg_file[rs1] < reg_file[rs2]) ? 1 : 0; // SLTU
            break;
          case 0b111:
            wb_data = reg_file[rs1] & reg_file[rs2]; // AND
            break;
          case 0b110:
            wb_data = reg_file[rs1] | reg_file[rs2]; // OR
            break;
          case 0b100:
            wb_data = reg_file[rs1] ^ reg_file[rs2]; // XOR
            break;
          case 0b001:
            wb_data = ((reg_file[rs1]) << (reg_file[rs2] & 0b111111)); // SLL
            break;
          case 0b101:
            wb_data = reg_file[rs1] >> (reg_file[rs2] & 0b111111); // SRL
            break;
          default:
            break;
          }
          reg_file[rd] = wb_data;
        }
        else if (func7 == 0b0000001)
        {
          switch (func3)
          {
          case 0b000: // MUL
            mult_temp = reg_file[rs1] * reg_file[rs2];
            reg_file[rd] = (mult_temp)&MASK64;
            break;
          case 0b001: // MULH
            mult_temp = ((__uint128_t)signed_value(reg_file[rs1]) * (__uint128_t)signed_value(reg_file[rs2]));
            reg_file[rd] = ((mult_temp) >> 64) & MASK64;
            break;
          case 0b010: // MULHSU
            mult_temp = ((__uint128_t)signed_value(reg_file[rs1]) * (__uint128_t)reg_file[rs2]);
            reg_file[rd] = ((mult_temp) >> 64) & MASK64;
            break;
          case 0b011: // MULHU
            mult_temp = (__uint128_t)reg_file[rs1] * (__uint128_t)reg_file[rs2];
            reg_file[rd] = ((mult_temp) >> 64) & MASK64;
            break;
          case 0b100: // DIV
            reg_file[rd] = (uint64_t)divi<int64_t>(signed_value(reg_file[rs1]), signed_value(reg_file[rs2]), 0);
            break;
          case 0b101: // DIVU
            reg_file[rd] = divi<uint64_t>(reg_file[rs1], reg_file[rs2], 1);
            break;
          case 0b110: // REM
            reg_file[rd] = (uint64_t)divi<int64_t>(signed_value(reg_file[rs1]), signed_value(reg_file[rs2]), 2);
            break;
          case 0b111: // REMU
            reg_file[rd] = divi<uint64_t>(reg_file[rs1], reg_file[rs2], 3);
            break;
          }
        }
        else if (func7 == 0b0100000)
        {
          switch (func3)
          {
          case 0b000: // SUB
            wb_data = reg_file[rs1] - reg_file[rs2];
            break;
          case 0b101: // SRA
            wb_data = reg_file[rs1];
            for (itr = 0; itr < (reg_file[rs2] & 0b111111); itr++)
            {
              wb_data = ((wb_data & ((1llu) << 63)) | ((wb_data) >> (1)));
            }
            break;
          default:
            break;
          }
          reg_file[rd] = wb_data;
        }
        break;
      case rops64:
        if (func7 == 0b0000000)
        {
          switch (func3)
          {
          case 0b000: // ADDW
            wb_data = sign_extend<uint64_t>(((reg_file[rs1] + reg_file[rs2]) & MASK32), 32);
            break;
          case 0b001: // SLLW
            wb_data = sign_extend<uint64_t>(((reg_file[rs1]) << ((reg_file[rs2]) & 0b11111)) & MASK32, 32);
            break;
          case 0b101: // SRLW
            wb_data = sign_extend<uint64_t>((reg_file[rs1] & MASK32) >> ((reg_file[rs2]) & 0b11111), 32);
            break;
          }
          reg_file[rd] = wb_data;
        }
        else if (func7 == 0b0000001)
        {
          // outdata << hex << PC-4 << endl;
          switch (func3)
          {
          case 0b000: // MULW
            wb_data = sign_extend<uint64_t>(((reg_file[rs1] & MASK32) * (reg_file[rs2] & MASK32)) & MASK32, 32);
            break;
          case 0b100: // DIVW
            wb_data = sign_extend<uint64_t>(MASK32 & ((uint64_t)divi32<int32_t>(signed_value32(reg_file[rs1] & MASK32), signed_value32(reg_file[rs2] & MASK32), 0)), 32);
            break;
          case 0b101: // DIVUW
            wb_data = sign_extend<uint64_t>((uint64_t)divi32<uint32_t>(reg_file[rs1], reg_file[rs2], 1), 32);
            break;
          case 0b110: // REMW
            wb_data = sign_extend<uint64_t>(((uint64_t)divi32<int32_t>(signed_value32(reg_file[rs1]), signed_value32(reg_file[rs2]), 2) & MASK32), 32);
            break;
          case 0b111: // REMUW
            wb_data = sign_extend<uint64_t>((uint64_t)divi32<uint32_t>(reg_file[rs1], reg_file[rs2], 3), 32);
            break;
          }
          reg_file[rd] = wb_data;
        }
        else if (func7 == 0b0100000)
        {
          switch (func3)
          {
          case 0b000: // SUBW
            wb_data = sign_extend<uint64_t>((((reg_file[rs1] & MASK32) - (reg_file[rs2] & MASK32)) & MASK32), 32);
            break;
          case 0b101: // SRAW
            wb_data = reg_file[rs1] & MASK32;
            for (itr = 0; itr < (reg_file[rs2] & 0b11111); itr++)
            {
              wb_data = ((wb_data & (1llu << 31)) | ((wb_data) >> (1)));
            }
            wb_data = sign_extend<uint64_t>(wb_data, 32);
            break;
          }
          reg_file[rd] = wb_data;
        }
        break;
      case amo:
        if (func3 == 0b010)
        { // AMO.W-32
          load_addr = reg_file[rs1] & 0x00000000ffffffff;
          if (load_addr > DRAM_BASE & load_addr < DRAM_BASE + 0x9000000)
          {
            load_addr = load_addr;
          }
          else
          {
            printf("illegal access\n");
            exit(0);
          }
          load_data = memory.at((load_addr - DRAM_BASE) / 8);
          switch (amo_op)
          {
          case 0b00010: // LR.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              STORE_ADDR_MISSALIG = true;
              mtval = load_addr;
            }
            else
            {
              ret_data = sign_extend<uint64_t>(wb_data & MASK32, 32);
              amo_reserve_valid = true;
              amo_reserve_addr = load_addr;
            }
            break;
          case 0b00011: // SC.W
            if (amo_reserve_valid && (reg_file[rs1] == amo_reserve_addr))
            {
              store_data = reg_file[rs2] & MASK32;
              ls_success = store_word(load_addr, load_data, store_data, wb_data);
              if (!ls_success)
              {
                STORE_ADDR_MISSALIG = true;
                mtval = load_addr;
                ret_data = 1;
              }
              else
              {
                memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
                ret_data = 0;
              }
            }
            else
            {
              ret_data = 1;
            }
            amo_reserve_addr = 0;
            amo_reserve_valid = false;
            break;
          case 0b00001: // AMOSWAP.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = reg_file[rs2] & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b00000: // AMOADD.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = ((wb_data & MASK32) + (reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b00100: // AMOXOR.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = ((wb_data & MASK32) ^ (reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b01100: // AMOAND.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = ((wb_data & MASK32) & (reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b01000: // AMOOR.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = ((wb_data & MASK32) | (reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b10000: // AMOMIN.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = min(signed_value32(wb_data & MASK32), signed_value32(reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b10100: // AMOMAX.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = max(signed_value32(wb_data & MASK32), signed_value32(reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b11000: // AMOMINU.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = min((wb_data & MASK32), (reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          case 0b11100: // AMOMAXU.W
            ls_success = load_word(load_addr, load_data, wb_data);
            if (!ls_success)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
              wb_data = max((wb_data & MASK32), (reg_file[rs2] & MASK32)) & MASK32;
              ls_success = store_word(load_addr, load_data, wb_data, store_data);
              memory.at((load_addr - DRAM_BASE) / 8) = store_data;
            }
            break;
          default:
            break;
          }
          reg_file[rd] = ret_data;
        }
        else if (func3 == 0b011)
        { // AMO.D-64
          load_addr = reg_file[rs1] & 0x00000000ffffffff;
          if (load_addr > DRAM_BASE & load_addr < DRAM_BASE + 0x9000000)
          {
            load_addr = load_addr;
          }
          else
          {
            printf("illegal access\n");
            exit(0);
          }
          wb_data = memory.at((load_addr - DRAM_BASE) / 8);
          switch (amo_op)
          {
          case 0b00010: // LR.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              amo_reserve_valid64 = true;
              amo_reserve_addr64 = reg_file[rs1];
            }
            break;
          case 0b00011: // SC.D
            if (amo_reserve_valid64 && (reg_file[rs1] == amo_reserve_addr64))
            {
              store_data = reg_file[rs2];
              if ((load_addr % 8) != 0)
              {
                mtval = load_addr;
                STORE_ADDR_MISSALIG = true;
              }
              else
              {
                memory.at((load_addr - DRAM_BASE) / 8) = store_data;
                ret_data = 0;
              }
            }
            else
            {
              ret_data = 1;
            }
            amo_reserve_addr64 = 0;
            amo_reserve_valid64 = false;
            break;
          case 0b00001: // AMOSWAP.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = reg_file[rs2];
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b00000: // AMOADD.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = (wb_data + reg_file[rs2]);
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b00100: // AMOXOR.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = (wb_data ^ reg_file[rs2]);
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b01100: // AMOAND.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = (wb_data & reg_file[rs2]);
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b01000: // AMOOR.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = (wb_data | reg_file[rs2]);
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b10000: // AMOMIN.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = min(signed_value(wb_data), signed_value(reg_file[rs2]));
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b10100: // AMOMAX.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = max(signed_value(wb_data), signed_value(reg_file[rs2]));
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b11000: // AMOMINU.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = min(wb_data, reg_file[rs2]);
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          case 0b11100: // AMOMAXU.D
            if ((load_addr % 8) != 0)
            {
              mtval = load_addr;
              STORE_ADDR_MISSALIG = true;
            }
            else
            {
              ret_data = wb_data;
              wb_data = max(wb_data, reg_file[rs2]);
              memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
            }
            break;
          default:
            break;
          }
          reg_file[rd] = ret_data;
        }
        break;
      case fence:
        break;
      case systm:
        switch (func3)
        {
        case 0b001: // CSRRW
          csr_data = csr_read(imm11_0);
          if (csr_read_success)
          {
            store_data = reg_file[rs1];
            csr_bool = csr_write(imm11_0, store_data);
            if (!csr_bool)
            {
              mtval = instruction;
              ILL_INS = true;
            }
            else if (rd != 0)
            {
              reg_file[rd] = csr_data;
            }
          }
          else
          {
            mtval = instruction;
            ILL_INS = true;
          }
          break;
        case 0b010: // CSRRS
          csr_data = csr_read(imm11_0);
          if (csr_read_success)
          {
            // if((imm11_0==CYCLE || imm11_0==INSTRET) && rd==0 && rs1 == 0) {
            //     break;
            // }
            store_data = reg_file[rs1];
            store_data = (store_data | csr_data);
            csr_bool = csr_write(imm11_0, store_data);
            if (!csr_bool)
            {
              mtval = instruction;
              ILL_INS = true;
            }
            else
            {
              reg_file[rd] = csr_data;
            }
          }
          else
          {
            mtval = instruction;
            ILL_INS = true;
          }
          break;
        case 0b011: // CSRRC
          csr_data = csr_read(imm11_0);
          if (csr_read_success)
          {
            // if(imm11_0==CYCLE || imm11_0==INSTRET) {
            //     reg_file[rd] = csr_data;
            // } else {
            store_data = reg_file[rs1];
            store_data = (csr_data & (MASK64 - store_data));
            csr_bool = csr_write(imm11_0, store_data);
            if (!csr_bool)
            {
              mtval = instruction;
              ILL_INS = true;
            }
            else
            {
              reg_file[rd] = csr_data;
            }
            // }
          }
          else
          {
            mtval = instruction;
            ILL_INS = true;
          }
          break;
        case 0b101: // CSRRWI
          csr_data = csr_read(imm11_0);
          if (csr_read_success)
          {
            csr_bool = csr_write(imm11_0, rs1);
            if (!csr_bool)
            {
              mtval = instruction;
              ILL_INS = true;
            }
            else if (rd != 0)
            {
              reg_file[rd] = csr_data;
            }
          }
          else
          {
            mtval = instruction;
            ILL_INS = true;
          }
          break;
        case 0b110: // CSRRSI
          csr_data = csr_read(imm11_0);
          if (csr_read_success)
          {
            // if((imm11_0==CYCLE || imm11_0==INSTRET) && rd==0 && rs1 == 0) {
            //     break;
            // }
            store_data = (rs1 | csr_data);
            csr_bool = csr_write(imm11_0, store_data);
            if (!csr_bool)
            {
              mtval = instruction;
              ILL_INS = true;
            }
            else
            {
              reg_file[rd] = csr_data;
            }
          }
          else
          {
            mtval = instruction;
            ILL_INS = true;
          }
          break;
        case 0b111: // CSRRCI
          csr_data = csr_read(imm11_0);
          if (csr_read_success)
          {
            // if(imm11_0==CYCLE || imm11_0==INSTRET) {
            //     reg_file[rd] = csr_data;
            // }
            // else {
            store_data = (csr_data & (MASK64 - rs1));
            csr_bool = csr_write(imm11_0, store_data);
            if (!csr_bool)
            {
              mtval = instruction;
              ILL_INS = true;
            }
            else
            {
              reg_file[rd] = csr_data;
            }
            // }
          }
          else
          {
            mtval = instruction;
            ILL_INS = true;
          }
          break;
        case 0b000:
          switch (imm11_0)
          {
          case 0: // ecall
            PC = excep_function(PC, CAUSE_MACHINE_ECALL, CAUSE_SUPERVISOR_ECALL, CAUSE_USER_ECALL, cp);
            break;
          case 1: // ebreak
            EBREAK = true;
            break;
          case 770: // mret
            PC = mepc;
            cp = (plevel_t)mstatus.mpp;
            mstatus.mie = mstatus.mpie; // 1; ?
            mstatus.mpp = 0b00;         // setting to umode (least privilage mode)
            mstatus.mpie = 1;           // 0; ?
            break;
          default:
            break;
          }
          break;
        default:
          break;
        }
        break;
      default:
        break;
      }
    }
    //printf("leon load_address: %016lx, data: %lu , core: %lu , core: %lu \n", load_addr, load_data , rd ,reg_file[rd]);
  }
};