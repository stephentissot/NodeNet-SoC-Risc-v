ENTRY(_start)

MEMORY
{
    RAM(rw)    : ORIGIN = 0x00010000, LENGTH = 64K
    SDRAM(rwx) : ORIGIN = 0x20000000, LENGTH = 8M
}

SECTIONS
{
    .text :
    {
        *(.text*)
    } > SDRAM

    .rodata :
    {
        *(.rodata*)
    } > SDRAM

    .init_array :
    {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    } > SDRAM

    .fini_array :
    {
        KEEP(*(.fini_array*))
    } > SDRAM

    .data :
    {
        __data_start = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        __data_end = .;
    } > SDRAM

    __data_load = LOADADDR(.data);

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        . = ALIGN(4);
        __bss_end = .;
    } > SDRAM

    end = .;

    _stack_top = ORIGIN(RAM) + LENGTH(RAM);

    .sdram (NOLOAD) :
    {
        _sdram_start = .;
        *(.sdram .sdram.*)
        _sdram_end = .;
    } > SDRAM
}
