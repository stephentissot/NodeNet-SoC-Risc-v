.section .text
.global _start

_start:
    j _reset_entry
    j _app_irq_entry

_reset_entry:

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

    call main

loop:
    j loop

_app_irq_entry:
    addi sp, sp, -128
    sw ra, 0(sp)
    sw gp, 4(sp)
    sw tp, 8(sp)
    sw t0, 12(sp)
    sw t1, 16(sp)
    sw t2, 20(sp)
    sw s0, 24(sp)
    sw s1, 28(sp)
    sw a0, 32(sp)
    sw a1, 36(sp)
    sw a2, 40(sp)
    sw a3, 44(sp)
    sw a4, 48(sp)
    sw a5, 52(sp)
    sw a6, 56(sp)
    sw a7, 60(sp)
    sw s2, 64(sp)
    sw s3, 68(sp)
    sw s4, 72(sp)
    sw s5, 76(sp)
    sw s6, 80(sp)
    sw s7, 84(sp)
    sw s8, 88(sp)
    sw s9, 92(sp)
    sw s10, 96(sp)
    sw s11, 100(sp)
    sw t3, 104(sp)
    sw t4, 108(sp)
    sw t5, 112(sp)
    sw t6, 116(sp)

    li t0, 0x20000004
    jalr ra, t0, 0

    lw ra, 0(sp)
    lw gp, 4(sp)
    lw tp, 8(sp)
    lw t0, 12(sp)
    lw t1, 16(sp)
    lw t2, 20(sp)
    lw s0, 24(sp)
    lw s1, 28(sp)
    lw a0, 32(sp)
    lw a1, 36(sp)
    lw a2, 40(sp)
    lw a3, 44(sp)
    lw a4, 48(sp)
    lw a5, 52(sp)
    lw a6, 56(sp)
    lw a7, 60(sp)
    lw s2, 64(sp)
    lw s3, 68(sp)
    lw s4, 72(sp)
    lw s5, 76(sp)
    lw s6, 80(sp)
    lw s7, 84(sp)
    lw s8, 88(sp)
    lw s9, 92(sp)
    lw s10, 96(sp)
    lw s11, 100(sp)
    lw t3, 104(sp)
    lw t4, 108(sp)
    lw t5, 112(sp)
    lw t6, 116(sp)
    addi sp, sp, 128
    .word 0x0400000b
