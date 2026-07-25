ENTRY(_start)

MEMORY
{
    ROM(rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    RAM(rw)  : ORIGIN = 0x00010000, LENGTH = 64K
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
}