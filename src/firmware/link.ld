ENTRY(_start)

MEMORY
{
    ROM(rx)   : ORIGIN = 0x00000000, LENGTH = 64K
    RAM(rw)   : ORIGIN = 0x00010000, LENGTH = 64K
    /* SDRAM: 8 MB total
     * Current implementation note:
     *   NodeNet485 uses MMIO mailboxes and does not consume external SDRAM.
     *   The full 0x20000000-0x207FFFFF range is therefore available to firmware.
     */
    SDRAM(rw) : ORIGIN = 0x20000000, LENGTH = 8M
}

SECTIONS
{
    .text :
    {
        *(.text*)
    } > ROM

    .rodata :
    {
        *(.rodata*)
    } > ROM

    /* C++ global constructors/destructors (function pointer arrays) */
    .init_array :
    {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    } > ROM

    .fini_array :
    {
        KEEP(*(.fini_array*))
    } > ROM

    .data :
    {
        *(.data*)
    } > RAM

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        __bss_end = .;
    } > RAM

    end = .;  /* Symbol for sbrk() — marks end of BSS/start of heap */

    _stack_top = ORIGIN(RAM) + LENGTH(RAM);

    /* SDRAM region — placed at 0x20000000, NOT initialized at boot.
     * The SDRAM controller needs ~200 µs to initialize after reset;
     * access this region only after sdram_ready() returns true.
     *
     * Usage: SDRAM_DATA uint8_t my_buffer[1024 * 1024];
     *        SDRAM_DATA StaticJsonDocument<65536> jsonDoc;
     */
    .sdram (NOLOAD) :
    {
        _sdram_start = .;
        *(.sdram .sdram.*)
        _sdram_end = .;
    } > SDRAM
}