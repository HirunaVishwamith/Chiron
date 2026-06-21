// hart_trap.inc — trap entry: exception + interrupt vectoring
//
// Fragment of class hart (see hart.h) — included inside the class body;
// not a standalone header. Kept separate to keep hart.h readable.

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
