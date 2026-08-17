.section .text
.global _start

.macro app_spin_delay cycles
    li   t2, \cycles
.Lapp_spin_\@:
    addi t2, t2, -1
    bnez t2, .Lapp_spin_\@
.endm

.macro app_d2_checkpoint count
    li   t0, 0x10000000
    li   t3, \count
.Lapp_d2_count_\@:
    li   t1, 1
    sw   t1, 0(t0)
    app_spin_delay 260000
    sw   zero, 0(t0)
    app_spin_delay 220000
    addi t3, t3, -1
    bnez t3, .Lapp_d2_count_\@
    app_spin_delay 420000
.endm

.macro app_d2_heartbeat
    li   t0, 0x10000000
.Lapp_d2_heartbeat_\@:
    li   t1, 1
    sw   t1, 0(t0)
    app_spin_delay 260000
    sw   zero, 0(t0)
    app_spin_delay 260000
    j    .Lapp_d2_heartbeat_\@
.endm

_start:

    # App checkpoint 1: entered _start after bootloader handoff.
    app_d2_checkpoint 1

    # Temporary bring-up switch:
    # 1 = skip .data/.bss/.init_array and jump directly to main for diagnosis.
    # This is safe for the current minimal blink firmware, which does not rely
    # on initialized globals or constructors before entering main().
    # 0 = normal startup sequence.
    .set BOOT_FAST_ENTRY, 1
    .set BOOT_LOCAL_HEARTBEAT, 0

    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la sp, _stack_top

    # App checkpoint 2: gp/sp initialized.
    app_d2_checkpoint 2

    .if BOOT_FAST_ENTRY
    # App checkpoint 3: fast path reached just before C main.
    app_d2_checkpoint 3

    .if BOOT_LOCAL_HEARTBEAT
    # Test 1: pure-assembly heartbeat from SDRAM, with no C runtime and no main.
    app_d2_heartbeat
    .endif

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

    # App checkpoint 3: full startup reached just before C main.
    app_d2_checkpoint 3

    call main

loop:
    j loop