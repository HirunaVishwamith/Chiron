// hart_memory.inc — memory access: load/store width + alignment
//
// Fragment of class hart (see hart.h) — included inside the class body;
// not a standalone header. Kept separate to keep hart.h readable.

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
