.section .text
.global _start

_start:

    # Post-jump checkpoint A: D2 ON.
    # Keep only one MMIO target here to avoid false stalls during diagnosis.
    li   t0, 0x10000000
    li   t1, 1
    sw   t1, 0(t0)

    # Temporary bring-up switch:
    # 1 = skip .data/.bss/.init_array and jump directly to main for diagnosis.
    # 0 = normal startup sequence.
    .set BOOT_FAST_ENTRY, 0

    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la sp, _stack_top

    .if BOOT_FAST_ENTRY
    # Checkpoint FAST: D2 OFF just before entering C main.
    li   t0, 0x10000000
    sw   zero, 0(t0)
    call main
1:  j 1b
    .endif

    # Copy .data section from ROM (LMA) to RAM (VMA).
    # Required for global objects initialized with compile-time constants
    # (GCC emits them as static data in .data, not via .init_array).
    la   a0, __data_load   # source: ROM address
    la   a1, __data_start  # dest:   RAM address
    la   a2, __data_end
.Ldata_loop:
    beq  a1, a2, .Ldata_done
    lw   t0, 0(a0)
    sw   t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j    .Ldata_loop
.Ldata_done:

    # Zero-initialize .bss section (required for static local guard variables and
    # zero-initialized globals; FPGA BRAM may not start at 0 after reconfiguration)
    la   a0, __bss_start
    la   a1, __bss_end
.Lbss_loop:
    beq  a0, a1, .Lbss_done
    sw   zero, 0(a0)
    addi a0, a0, 4
    j    .Lbss_loop
.Lbss_done:

    # Call global C++ constructors (.init_array)
    # Required for any global/static objects with constructors (e.g. ArduinoJson)
    la   a0, __init_array_start
    la   a1, __init_array_end
.Lctor_loop:
    beq  a0, a1, .Lctor_done
    lw   t0, 0(a0)
    jalr t0
    addi a0, a0, 4
    j    .Lctor_loop
.Lctor_done:

    # Post-jump checkpoint B: D2 OFF.
    # Means startup reached just before calling C main().
    li   t0, 0x10000000
    sw   zero, 0(t0)

    call main

loop:
    j loop
