/*======================================================================*/
/* Proxy kernel linker script                                           */
/*  Hiruna Vishwamith
    UOM
    10/03/2025                                                          */
/*======================================================================*/
/* This is the linker script used when building the proxy kernel. */

/*----------------------------------------------------------------------*/
/* Setup                                                                */
/*----------------------------------------------------------------------*/

/* The OUTPUT_ARCH command specifies the machine architecture where the
   argument is one of the names used in the BFD library. More
   specifically one of the entires in bfd/cpu-mips.c */

OUTPUT_ARCH( "riscv" )

ENTRY(_start)

/*----------------------------------------------------------------------*/
/* Memory Definitions                                                   */
/*----------------------------------------------------------------------*/

MEMORY
{
    ram (rwx) : ORIGIN = 0x0, LENGTH = 64K  /* Define RAM starting at 0x10000000 with 64KB size */
}

/* ram (rwx) : ORIGIN = 0x10000000, LENGTH = 64K */

/*----------------------------------------------------------------------*/
/* Sections                                                             */
/*----------------------------------------------------------------------*/

SECTIONS
{

  /* Start at the beginning of RAM */
  /* . = 0x10000000; */
  . = 0x0;


  /* text: test code section */
  .text.init : { crt.o(.text) } > ram

  /* Main text section for other code */
  .text : { *(.text) } > ram

  /* Small data section for global pointer */
  .sdata : {
      _gp = . + 0x800;
      *(.sdata .sdata.*)
  } > ram

  /* Data section */
  .data : ALIGN(8) {
      _sdata_start = .;  /* Start of .data */
      *(.data .data.*)   /* Include all initialized data */
      _sdata_end = .;    /* End of .data */
  } > ram

  /* BSS section for uninitialized data */
  .bss : ALIGN(8) {
      _sbss = .;         /* Start of .bss */
      *(.bss .bss.*)     /* Include all uninitialized data */
      *(COMMON)
      _ebss = .;         /* End of .bss */
  } > ram

  /* End of uninitialized data segment */
  _end = .;


}

