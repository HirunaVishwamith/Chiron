//**************************************************************************
// Multi-threaded booting rootine
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 10/03/2025
//
//
//--------------------------------------------------------------------------
// booting sequence
//
// 1. initiaalize the global pointer
// 2. initialize the trap vector
// 3. zeroout the bss - (initialize the result matrix)
// 4. reserve stack and tls are for each core
// 5. call thread entry function

//--------------------------------------------------------------------------

#include "encoding.h"


#ifndef NUM_CORES
#define NUM_CORES 1 // Override via -DNUM_CORES=N on the compiler command line.
#endif
//#  define DATA_BASE 0x10040000
 #define DATA_BASE 0x80040000
# define STACK_SIZE (1<<17)
# define TLS_SIZE   (1<<17)
#define REGION_SHIFT 18 // (STACK_SIZE + TLS_SIZE) 
#define UART_BASE 0x40600004
// #define UART_BASE 0xe0001030


  # .text
  .section ".text.init" ## code starts here in the elf file
  .globl _start
  .type   _start, @function

_start:

  # initialize global pointer
.option push
.option norelax
	la		gp,_gp
.option pop


  li  x1, 0
  li  x2, 0
  li  x3, 0
  li  x4, 0
  li  x5, 0
  li  x6, 0
  li  x7, 0
  li  x8, 0
  li  x9, 0
  li  x10,0
  li  x11,0
  li  x12,0
  li  x13,0
  li  x14,0
  li  x15,0
  li  x16,0
  li  x17,0
  li  x18,0
  li  x19,0
  li  x20,0
  li  x21,0
  li  x22,0
  li  x23,0
  li  x24,0
  li  x25,0
  li  x26,0
  li  x27,0
  li  x28,0
  li  x29,0
  li  x30,0
  li  x31,0


  # make sure XLEN agrees with compilation choice
  li t0, 1
  slli t0, t0, 31
  bgez t0, 1f  # if t0 >= 0, branch forward to label 1

2:                    # Local label '2'
  li a0, 1            # Load Immediate: puts value 1 into register a0
  li   t1, UART_BASE
  sw   a0, 0(t1)      # Store Word: stores the value in a0 to address 'tohost', using t0 as temporary
  j 2b                # Jump backwards (b = backward) to label 2
1:


  # initialize trap vector
  la t0, trap_entry       # Load Address of trap_entry into t0
  csrw mtvec, t0         # Write t0 to mtvec CSR (Control/Status Register)


  # get core id
  csrr a0, mhartid
  la x12, hart_init_sync
  li x6,1
  sb x6,0(x12)
  fence

    # Only core 0 should perform data initialization
    bnez a0,sync_all_harts #If hard id !=0, skip initialization

    # --- Core 0 Initialization --- #
    # Clear the bss segmnet #

    # Zero out the .bss section
    la t5, _sbss # Start address of .bss
    la t6, _ebss # End address of .bss

clear_bss:
    bge t5,t6, finish_bss # If reached the end of .bss, finish initialization
    sw      x0, 0(t5)
    addi    t5, t5, 4
    j       clear_bss

finish_bss:
    la a1, hart_init_sync
    sw x0, 0(a1)    # store zero into hart_init_sync to relesase all the harts

    # End of initialization for core 0

sync_all_harts:

    # wait until the word at hart_init_sync is Zero

    la a1, hart_init_sync
    lw a2, 0(a1)
    bnez a2, sync_all_harts



  # load number of cores into a1
  li a1, NUM_CORES

  # Define constants:
  # REGION_SIZE = TLS_SIZE (0x100) + STACK_SIZE (0x1000) = 0x1100
  li      t3, 0x1
  sll     t2, t3, REGION_SHIFT  # t2 = 0x1100

  # Compute offset = hart id * REGION_SIZE
  mul     t0, a0, t2        # t0 = hart id * 0x1100

  # Compute TLS base = DATA_BASE + offset.
  li      t1, DATA_BASE     # t1 = DATA_BASE (0x10000000)
  add     t1, t1, t0        # t1 = TLS base for this core

  # Set the thread pointer (x4, tp) to point to our TLS area.
  mv      tp, t1            # tp = TLS base

  # Compute the stack top for this core:
  # (stack grows downward, so we initialize sp to the top address of the per‑core region)

  add     t1, t1, t2     # t1 = TLS base + REGION_SIZE (i.e. end of region)
  mv      sp, t1                              # sp = stack pointer

  # (At this point each core has:
  #   its TLS area from [DATA_BASE + hart_id*0x1100, DATA_BASE + hart_id*0x1100 + 0x100)
  #   and its stack from [DATA_BASE + hart_id*0x1100 + 0x100, DATA_BASE + (hart_id+1)*0x1100),
  # with the stack pointer set to the top (highest address) of that core’s stack.)


  # a0 and a1 are caller-saved registers variable cid is stored in a0 and nc is on a1

  # jump to _init with cid in a0 and nc in a1
  jal thread_entry // Jump and Link to _init

  .align 2
trap_entry:
  addi sp, sp, -272

  sd x1, 1*8(sp)
  sd x2, 2*8(sp)
  sd x3, 3*8(sp)
  sd x4, 4*8(sp)
  sd x5, 5*8(sp)
  sd x6, 6*8(sp)
  sd x7, 7*8(sp)
  sd x8, 8*8(sp)
  sd x9, 9*8(sp)
  sd x10, 10*8(sp)
  sd x11, 11*8(sp)
  sd x12, 12*8(sp)
  sd x13, 13*8(sp)
  sd x14, 14*8(sp)
  sd x15, 15*8(sp)
  sd x16, 16*8(sp)
  sd x17, 17*8(sp)
  sd x18, 18*8(sp)
  sd x19, 19*8(sp)
  sd x20, 20*8(sp)
  sd x21, 21*8(sp)
  sd x22, 22*8(sp)
  sd x23, 23*8(sp)
  sd x24, 24*8(sp)
  sd x25, 25*8(sp)
  sd x26, 26*8(sp)
  sd x27, 27*8(sp)
  sd x28, 28*8(sp)
  sd x29, 29*8(sp)
  sd x30, 30*8(sp)
  sd x31, 31*8(sp)

  csrr a0, mcause
  csrr a1, mepc
  mv a2, sp
  jal thread_entry
  csrw mepc, a0

  # Remain in M-mode after eret
  li t0, MSTATUS_MPP
  csrs mstatus, t0

  ld x1, 1*8(sp)
  ld x2, 2*8(sp)
  ld x3, 3*8(sp)
  ld x4, 4*8(sp)
  ld x5, 5*8(sp)
  ld x6, 6*8(sp)
  ld x7, 7*8(sp)
  ld x8, 8*8(sp)
  ld x9, 9*8(sp)
  ld x10, 10*8(sp)
  ld x11, 11*8(sp)
  ld x12, 12*8(sp)
  ld x13, 13*8(sp)
  ld x14, 14*8(sp)
  ld x15, 15*8(sp)
  ld x16, 16*8(sp)
  ld x17, 17*8(sp)
  ld x18, 18*8(sp)
  ld x19, 19*8(sp)
  ld x20, 20*8(sp)
  ld x21, 21*8(sp)
  ld x22, 22*8(sp)
  ld x23, 23*8(sp)
  ld x24, 24*8(sp)
  ld x25, 25*8(sp)
  ld x26, 26*8(sp)
  ld x27, 27*8(sp)
  ld x28, 28*8(sp)
  ld x29, 29*8(sp)
  ld x30, 30*8(sp)
  ld x31, 31*8(sp)

  addi sp, sp, 272
  mret

	.size  _start, .-_start

	.data

hart_init_sync:
	.word 1							# hart zero will set this to 0 when the init logic has completed