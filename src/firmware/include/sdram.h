/**
 * sdram.h — SDRAM access helpers for Colorlight i9 SoC
 *
 * The M12L64322A SDRAM (8 MB at 0x20000000) is controlled by the wb_sdram
 * Wishbone peripheral. After FPGA reset, the controller runs a ~200 µs
 * initialization sequence (power-on hold + precharge + refresh + mode load)
 * before the SDRAM is accessible.
 *
 * SDRAM Allocation:
 *   0x20000000–0x200FFFFF (1 MB) — Reserved for NodeNet485 (TX + RX FIFOs)
 *   0x20100000–0x207FFFFF (7 MB) — Available for application (SDRAM_DATA variables)
 *
 * Usage — place a variable in SDRAM:
 *
 *   #include "sdram.h"
 *
 *   SDRAM_DATA uint8_t  large_buffer[512 * 1024];  // Placed at 0x20100000+
 *   SDRAM_DATA uint32_t modbus_log[4096];
 *   SDRAM_DATA StaticJsonDocument<65536> json_doc;  // C++ only
 *
 * The linker assigns exact addresses automatically (starts at 0x20100000).
 * Variables are placed in order of declaration across translation units.
 *
 * NOTE: Do NOT access SDRAM_DATA variables before calling sdram_wait_ready().
 * NOTE: NodeNet485 buffers are at 0x20000000 and handled separately.
 */

#ifndef SDRAM_H
#define SDRAM_H

#include <stdint.h>

/* ── Placement macro ──────────────────────────────────────────────────────── */

/** Place a variable in the external SDRAM (0x20000000–0x207FFFFF, 8 MB).
 *  The variable is NOT zero-initialized at boot (NOLOAD section).
 *  Zero-initialize explicitly if needed: memset(&my_var, 0, sizeof(my_var));
 */
#define SDRAM_DATA  __attribute__((section(".sdram")))

/* ── Linker-exported symbols ─────────────────────────────────────────────── */

/** Start and end of the .sdram section (set by the linker). */
extern char _sdram_start;
extern char _sdram_end;

/* ── SDRAM controller register ───────────────────────────────────────────── */

/* ── SDRAM Allocation ────────────────────────────────────────────────────── */

/** NodeNet485 reserved region (1 MB).
 *  TX FIFO: 0x20000000–0x2007FFFF (512 KB)
 *  RX FIFO: 0x20080000–0x200FFFFF (512 KB)
 *  Do NOT allocate SDRAM_DATA variables here; they will be placed in the
 *  application region automatically by the linker.
 */
#define SDRAM_NODENET_BASE  0x20000000UL
#define SDRAM_NODENET_SIZE  (1UL * 1024 * 1024)  // 1 MB reserved

/** Application region (7 MB) — where SDRAM_DATA variables are placed.
 *  PicoRV32 application code and SDRAM_DATA variables start here.
 *  Access this region safely; it does not conflict with NodeNet485 buffers.
 */
#define SDRAM_APP_BASE  0x20100000UL
#define SDRAM_APP_SIZE  (7UL * 1024 * 1024)  // 7 MB for application

/** Legacy: Base address of SDRAM window (now points to application region).
 *  For backwards compatibility, SDRAM_BASE now refers to the application area.
 *  Direct raw SDRAM access should use SDRAM_APP_BASE or SDRAM_NODENET_BASE.
 */
#define SDRAM_BASE  SDRAM_APP_BASE

/** Legacy: Total accessible SDRAM size (now 7 MB for application only).
 *  The full 8 MB is present on hardware, but 1 MB is reserved for NodeNet485.
 */
#define SDRAM_SIZE  SDRAM_APP_SIZE

/* ── Initialization wait ─────────────────────────────────────────────────── */

/**
 * sdram_wait_ready() — busy-wait until the SDRAM controller has completed
 * its power-on initialization sequence.
 *
 * The wb_sdram Wishbone controller needs ~200 µs (5000 cycles at 25 MHz) to
 * initialize the SDRAM after FPGA reset. During that time, any Wishbone
 * access to the SDRAM address window will be stalled (wb_ack never asserted),
 * effectively hanging the CPU.
 *
 * Strategy: perform a dummy write + read-back to a known SDRAM address.
 * The first access will stall until the controller is ready (init completes
 * before any wb_stb is acknowledged). Once the read-back succeeds the SDRAM
 * is operational.
 *
 * Call this once at the beginning of main(), before any SDRAM_DATA variable
 * is accessed.
 */
static inline void sdram_wait_ready(void)
{
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;

    /* Write a known pattern; the Wishbone stall mechanism ensures this
     * completes only once the SDRAM controller has finished its init. */
    *p = 0xA5A5A5A5UL;

    /* Dummy read to flush the pipeline. */
    (void)(*p);
}

/**
 * sdram_test() — basic write/read-back pattern test over the first N words.
 * Returns 0 on success, or the number of mismatches found.
 *
 * Example:
 *   uint32_t errors = sdram_test(1024);  // Test first 4 KB
 */
static inline uint32_t sdram_test(uint32_t num_words)
{
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
    uint32_t i, errors = 0;

    for (i = 0; i < num_words; i++)
        p[i] = i ^ 0xA5A5A5A5UL;

    for (i = 0; i < num_words; i++) {
        if (p[i] != (i ^ 0xA5A5A5A5UL))
            errors++;
    }

    return errors;
}

#endif /* SDRAM_H */
