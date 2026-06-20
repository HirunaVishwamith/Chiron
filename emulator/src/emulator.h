
/**
 * This header file contains a riscv-emulator implemented
 * as a class. This provides an easy way to use the same
 * emulator code for testing the two processors.
 */

#define MEM_SIZE 28
#define NUM_HARTS 4

/* EMULATOR INCLUDE HEADER FILES */

#include <vector>
#include <iostream>
#include "hart.h"

using namespace std;

class emulator
{
private:
vector<uint64_t> memory = vector<uint64_t>(1 << MEM_SIZE); // main memory
vector<hart> harts;


public:
  emulator():harts(NUM_HARTS,memory) // this Constructor will construct each harts
  {
    for(uint8_t i=0; i<NUM_HARTS; i++)
      harts[i].hart_init(memory,i);
  }

  void init(string image_name)
  {

    ifstream infile(image_name, ios::binary);
    printf("stepping\n");
    if (!infile.good())
    {
      exit(0);
    }

    // Get the file size by seeking to the end and then getting the position
    infile.seekg(0, ios::end);
    streampos fileSize = infile.tellg();
    infile.seekg(0, ios::beg);

    // Create a vector to store the binary data
    vector<unsigned long> byte_memory(fileSize);

    // Read the binary data into the vector
    infile.read(reinterpret_cast<char *>(byte_memory.data()), fileSize);

    infile.close();
    unsigned long pointer_end = (fileSize / 8) - 1;
    unsigned long long_jump = 0;
    for (const uint64_t data : byte_memory)
    {
      memory.at(long_jump) = (static_cast<unsigned long>(data));
      // printf("%lx\n", static_cast<unsigned long>(address));
      if ((long_jump++) >= pointer_end)
      {
        break;
      }
    }

    /// inittilize mem
  }

  void step()
  {
    for (auto &r : harts)
      r.hart_step(memory);

    // uint64_t loc0 = 10001600;
    // uint64_t loc1 = 10001604;
    // uint64_t db = 10000000;
    // int m0 = memory.at((loc0 - db) / 8);
    // int m1 = memory.at((loc1 - db) / 8);

    // printf("m0: %d m1: %d\n", m0, m1);
    // 0000000010001600

  }

  void step(int i){
    harts[i].hart_step(memory);
  }

  void set_interrupts(int i){
    harts[i].hart_set_interrupts(memory);
  }

 // void set_interrupts()
  //{
    //for (auto &r : harts)
    // r.hart_set_interrupts();
  //}

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

  vector<uint64_t> reg_file(int i){
    return harts[i].reg_file;
  }

  void set_register_with_value(__uint8_t rd,__uint64_t value,int i){
    return harts[i].set_register_with_value(rd,value);
  }

  int is_peripheral_read(int i){
    return harts[i].is_peripheral_read(memory);
  }



};