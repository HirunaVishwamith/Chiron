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
/* Link at the simulator/emulator RAM base (0x80000000). The image is loaded and
 * executed at 0x80000000, so the program MUST be linked there: absolute data
 * pointers (e.g. the `palette` array of `const char*`) are NOT relocated at load
 * time (bare metal, no loader). Linking at 0x0 leaves them pointing into
 * unmapped low memory -> empty strings -> black screen. */
MEMORY
{
    ram (rwx) : ORIGIN = 0x80000000, LENGTH = 256M  /* RAM base = DRAM_BASE/instructionBase */
}

/* Sections */
SECTIONS
{
    /* Start at RAM base */
    . = 0x80000000;

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
        *(COMMON)
        _ebss = .;         /* End of .bss */
    } > ram

    /* End of allocated memory */
    _end = .;
}