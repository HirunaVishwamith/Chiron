#pragma once

#define MEM_SIZE 28
#define NUM_HARTS 4

#include <vector>
#include <iostream>
#include "hart.h"
#include "clint.h"

class emulator
{
private:
  std::vector<uint64_t> memory = std::vector<uint64_t>(1 << MEM_SIZE);
  CLINT clint;
  std::vector<hart> harts;


public:
  emulator() : clint(NUM_HARTS), harts()
  {
    // Construct harts with reference to the shared CLINT (per-hart mtimecmp/msip)
    for (uint8_t i = 0; i < NUM_HARTS; i++) {
      harts.emplace_back(memory, clint);
    }
    for (uint8_t i = 0; i < NUM_HARTS; i++)
      harts[i].hart_init(memory, i);
  }

  void init(std::string image_name)
  {
    std::ifstream infile(image_name, std::ios::binary);
    if (!infile.good())
    {
      fprintf(stderr, "emulator: cannot open image '%s'\n", image_name.c_str());
      exit(1);
    }

    infile.seekg(0, std::ios::end);
    std::streampos fileSize = infile.tellg();
    infile.seekg(0, std::ios::beg);

    std::vector<unsigned long> byte_memory(fileSize);

    // Read the binary data into the vector
    infile.read(reinterpret_cast<char *>(byte_memory.data()), fileSize);

    infile.close();
    unsigned long pointer_end = (fileSize / 8) - 1;
    unsigned long long_jump = 0;
    for (const uint64_t data : byte_memory)
    {
      memory.at(long_jump) = (static_cast<unsigned long>(data));
      if ((long_jump++) >= pointer_end)
        break;
    }
  }

  void step()
  {
    clint.tick();
    for (auto &r : harts)
      r.hart_step(memory);
  }

  void step(int i){
    clint.tick();
    harts[i].hart_step(memory);
  }

  void set_interrupts(int i){
    // CLINT mtime/msip already updated by tick(); just refresh per-hart view
#ifdef LOCKSTEP
    harts[i].hart_set_interrupts(memory);
#else
    harts[i].hart_set_interrupts();
#endif
  }

  void set_interrupts(){
    clint.tick();
    for (auto &r : harts)
#ifdef LOCKSTEP
      r.hart_set_interrupts(memory);
#else
      r.hart_set_interrupts();
#endif
  }

  // Fine-grained control for better IPI/timer causality in the Linux golden run.
  // tick_only + per-hart step + deliver after each makes MSIP writes from one
  // hart visible to the target's top-of-step check with very low latency.
  // Uses advance() (step-driven mtime), not tick() (wall-clock) -- see the
  // comment on CLINT::advance() for why wall-clock mtime livelocks Linux SMP
  // boot in an interpretive multi-hart emulator.
  void tick_only() { clint.advance(); }
  void step_hart_only(int i) { harts[i].hart_step(memory); }
  void deliver_interrupts() {
#ifdef LOCKSTEP
    for (auto &r : harts) r.hart_set_interrupts(memory);
#else
    for (auto &r : harts) r.hart_set_interrupts();
#endif
  }

  void show_registers()
  {
    for (auto &r : harts)
      r.show_state();
  }

  __uint64_t fetch_long(__uint64_t offset) { return memory.at(offset / 8); }

  uint32_t get_instruction(int i){
    return harts[i].get_instruction(memory);
  }

  __uint64_t get_pc(int i){
    return harts[i].get_pc();
  }

  void show_state(int i){
    return harts[i].show_state();
  }

  uint64_t get_mstatus(int i){
    return harts[i].get_mstatus();
  }

  std::vector<uint64_t> reg_file(int i){
    return harts[i].reg_file;
  }

  void set_register_with_value(__uint8_t rd,__uint64_t value,int i){
    return harts[i].set_register_with_value(rd,value);
  }

  int is_peripheral_read(int i){
    return harts[i].is_peripheral_read(memory);
  }



};