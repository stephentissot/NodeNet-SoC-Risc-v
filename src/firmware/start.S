.section .text
.global _start

_start:

    la sp, _stack_top

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