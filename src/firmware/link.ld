ENTRY(_start)

MEMORY
{
    ROM(rx)   : ORIGIN = 0x00000000, LENGTH = 64K
    RAM(rw)   : ORIGIN = 0x00010000, LENGTH = 64K
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

    .data :
    {
        *(.data*)
    } > RAM

    .bss :
    {
        *(.bss*)
    } > RAM

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