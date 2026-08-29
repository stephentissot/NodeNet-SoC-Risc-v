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

    /* Anchor for gp-relative small data accesses (RISC-V relaxation). */
    PROVIDE(__global_pointer$ = __data_start + 0x800);

    __data_load = LOADADDR(.data);

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        . = ALIGN(4);
        __bss_end = .;
    } > SDRAM

    _stack_top = (ORIGIN(RAM) + LENGTH(RAM)) & ~15;

    .sdram (NOLOAD) :
    {
        . = ALIGN(8);
        _sdram_start = .;
        *(.sdram .sdram.*)
        . = ALIGN(8);
        _sdram_end = .;
    } > SDRAM

    /* Keep the C heap after the reserved .sdram pool/scratch area when the
     * application itself executes from SDRAM. Otherwise malloc/realloc-based
     * JsonDocument instances can overwrite g_sdram_json_pool and corrupt the
     * custom ArduinoJson allocator, which later surfaces as false NoMemory.
     */
    end = .;
    _end = .;
}
