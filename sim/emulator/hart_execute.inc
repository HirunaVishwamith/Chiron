// hart_execute.inc — instruction step: fetch, decode, execute, writeback
//
// Fragment of class hart (see hart.h) — included inside the class body;
// not a standalone header. Kept separate to keep hart.h readable.

  void hart_step(vector<uint64_t> &memory)
  {
    reg_file[0] = 0;


    gettimeofday(&tv, NULL);
    time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
    mtime = (uint64_t)(time_in_micros * 10);

    instruction = hart_fetch_instruction(PC,memory);

#ifndef LOCKSTEP
    // ISA test completion detection for standalone emulator
    if (instruction == 0x00000073 && reg_file[3] == 0x1 && reg_file[17] == 0x5d && reg_file[10] == 0x0) {
      std::cout << "All tests passed" << std::endl;
      disable_raw_mode();
      exit(0);
    }
    if (instruction == 0x00018513) {
      std::cout << "Tests failed" << std::endl;
      disable_raw_mode();
      exit(1);
    }
#endif

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
          if (!(load_addr > DRAM_BASE && load_addr < DRAM_BASE + 0x9000000))
          {
            printf("AMO.W: illegal access addr=%lx\n", load_addr);
            exit(1);
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
          if (!(load_addr > DRAM_BASE && load_addr < DRAM_BASE + 0x9000000))
          {
            printf("AMO.D: illegal access addr=%lx\n", load_addr);
            exit(1);
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
