/*======================================================================*/
/* Proxy kernel linker script                                           */
/*  Hiruna Vishwamith                                                   */
/*  UOM                                                                */
/*  10/03/2025                                                         */
/*======================================================================*/

/* Specify the entry point explicitly */
ENTRY(_start)

/* Target architecture */
OUTPUT_ARCH("riscv")

/* Memory Definitions */
MEMORY
{
    ram (rwx) : ORIGIN = 0x0, LENGTH = 16000K  /* RAM starts at 0x0, 64KB size */
}

/* Sections */
SECTIONS
{
    /* Start at address 0x0 */
    . = 0x0;

    /* Initial text section (boot code) with 4-byte alignment */
    .text.init : ALIGN(4) { crt.o(.text) } > ram

    /* Main text section with 4-byte alignment */
    .text : ALIGN(4) { *(.text) } > ram


    /* Small data section for global pointer with 4-byte alignment */
    .sdata : ALIGN(4) {
        _gp = . + 0x800;
        *(.sdata .sdata.*)
    } > ram

    /* Data section with 8-byte alignment */
    .data : ALIGN(8) {
        _sdata_start = .;  /* Start of .data */
        *(.data .data.*)   /* Initialized data */
        _sdata_end = .;    /* End of .data */
    } > ram

    /* BSS section with 8-byte alignment */
    .bss : ALIGN(8) {
        _sbss = .;         /* Start of .bss */
        *(.bss .bss.*)     /* Uninitialized data */
        /* .sbss/.scommon MUST be here. RISC-V puts small zero-initialised
           statics in .sbss, and omitting it left `count` and `sense` -- the
           shared barrier state in common/util.h -- at and just past _ebss,
           so crt.S's clear loop never zeroed them. The harness compensated by
           having EVERY hart zero them at thread entry, unsynchronised, which
           is the race Kairos found (see docs/research/12). */
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.scommon)
        *(COMMON)
        _ebss = .;         /* End of .bss */
    } > ram

    /* End of allocated memory */
    _end = .;
}