// hart_csr.inc — CSR file: register structs + csr_read / csr_write
//
// Fragment of class hart (see hart.h) — included inside the class body;
// not a standalone header. Kept separate to keep hart.h readable.

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
