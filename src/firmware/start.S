.section .text
.global _start

.extern nodenet_irq_dispatch

_start:
    j _reset_entry
    j _irq_entry

_reset_entry:

    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la sp, _stack_top

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
    # Keep loop state in callee-saved registers because constructors are free
    # to clobber a0-a7/t0-t6 under the RISC-V ABI.
    la   s0, __init_array_start
    la   s1, __init_array_end
.Lctor_loop:
    beq  s0, s1, .Lctor_done
    lw   t0, 0(s0)
    addi s0, s0, 4
    jalr t0
    j    .Lctor_loop
.Lctor_done:

    call main

loop:
    j loop

_irq_entry:
    addi sp, sp, -16
    sw ra, 12(sp)

    call nodenet_irq_dispatch

    lw ra, 12(sp)
    addi sp, sp, 16
    ret