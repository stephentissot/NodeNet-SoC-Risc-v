/**
 * sdram.h — SDRAM access helpers for Colorlight i9 SoC
 *
 * The M12L64322A SDRAM (8 MB at 0x20000000) is controlled by the wb_sdram
 * Wishbone peripheral. After FPGA reset, the controller runs a ~200 µs
 * initialization sequence (power-on hold + precharge + refresh + mode load)
 * before the SDRAM is accessible.
 *
 * SDRAM Allocation:
 *   0x20000000–0x207FFFFF (8 MB) — Available for application (SDRAM_DATA variables)
 *
 * Usage — place a variable in SDRAM:
 *
 *   #include "sdram.h"
 *
 *   SDRAM_DATA uint8_t  large_buffer[512 * 1024];  // Placed at 0x20000000+
 *   SDRAM_DATA uint32_t modbus_log[4096];
 *   SDRAM_DATA StaticJsonDocument<65536> json_doc;  // C++ only
 *
 * The linker assigns exact addresses automatically (starts at 0x20000000).
 * Variables are placed in order of declaration across translation units.
 *
 * NOTE: Do NOT access SDRAM_DATA variables before calling sdram_wait_ready().
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

/** Full SDRAM window exposed by wb_sdram. */
#define SDRAM_BASE  0x20000000UL
#define SDRAM_SIZE  (8UL * 1024 * 1024)

/* Backward-compatible aliases. */
#define SDRAM_APP_BASE  SDRAM_BASE
#define SDRAM_APP_SIZE  SDRAM_SIZE

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

typedef void (*sdram_status_cb_t)(uint8_t line, const char *text);

static inline uint32_t sdram_test_region(uint32_t byte_offset, uint32_t num_words, uint32_t salt)
{
    volatile uint32_t *p = (volatile uint32_t *)(SDRAM_BASE + byte_offset);
    uint32_t i;
    uint32_t errors = 0;

    for (i = 0; i < num_words; i++) {
        p[i] = (i * 2654435761UL) ^ salt;
    }

    for (i = 0; i < num_words; i++) {
        if (p[i] != ((i * 2654435761UL) ^ salt)) {
            errors++;
        }
    }

    return errors;
}

static inline uint32_t sdram_test_byte_pattern(uint32_t byte_offset)
{
    static const uint8_t expected[] = {
        0x40, 0x78, 0x69, 0x2A, 0x55, 0xAA, 0x13, 0x37,
        0x00, 0xFF, 0x1C, 0xE3, 0x5A, 0xA5, 0xC3, 0x3C,
        0x11, 0x22, 0x44, 0x88, 0xFE, 0xEF, 0x7D, 0xB6,
        0x09, 0x90, 0x24, 0x42, 0x66, 0x99, 0xDE, 0xAD
    };
    volatile uint8_t *p = (volatile uint8_t *)(SDRAM_BASE + byte_offset);
    uint32_t errors = 0;

    for (uint32_t i = 0; i < sizeof(expected); i++) {
        p[i] = expected[i];
    }

    for (uint32_t i = 0; i < sizeof(expected); i++) {
        if (p[i] != expected[i]) {
            errors++;
        }
    }

    return errors;
}

/* Real section-placement probe: this variable must be linked in .sdram. */
static SDRAM_DATA volatile uint32_t g_sdram_data_probe_words[16];

static inline uint32_t sdram_test_data_section_variable(void)
{
    static const uint8_t expected[sizeof(g_sdram_data_probe_words)] = {
        0x40, 0x78, 0x69, 0x2A, 0x55, 0xAA, 0x13, 0x37,
        0x00, 0xFF, 0x1C, 0xE3, 0x5A, 0xA5, 0xC3, 0x3C,
        0x11, 0x22, 0x44, 0x88, 0xFE, 0xEF, 0x7D, 0xB6,
        0x09, 0x90, 0x24, 0x42, 0x66, 0x99, 0xDE, 0xAD,
        0x5C, 0xC5, 0x81, 0x18, 0x3F, 0xF3, 0x7A, 0xA7,
        0x0F, 0xF0, 0x12, 0x21, 0x34, 0x43, 0x56, 0x65,
        0x89, 0x98, 0xAB, 0xBA, 0xCD, 0xDC, 0xEE, 0x6E,
        0x04, 0x40, 0x28, 0x82, 0x17, 0x71, 0x39, 0x93
    };
    static const uint32_t expected_words[16] = {
        0x2A697840UL, 0x3713AA55UL, 0xE31CFF00UL, 0x3CC3A55AUL,
        0x88442211UL, 0xB67DEFFEUL, 0x42249009UL, 0xADDE9966UL,
        0x1881C55CUL, 0xA77AF33FUL, 0x2112F00FUL, 0x65564334UL,
        0xBAAB9889UL, 0x6EEEDCCDUL, 0x82284004UL, 0x93397117UL
    };
    uint32_t errors = 0;
    const uintptr_t addr = (uintptr_t)&g_sdram_data_probe_words[0];
    const uintptr_t sdram_start = (uintptr_t)&_sdram_start;
    const uintptr_t sdram_end = (uintptr_t)&_sdram_end;

    /* Check both physical SDRAM window and linker section boundaries. */
    if (addr < (uintptr_t)SDRAM_BASE || addr >= (uintptr_t)(SDRAM_BASE + SDRAM_SIZE)) {
        errors++;
    }
    if (addr < sdram_start || addr >= sdram_end) {
        errors++;
    }

    for (uint32_t i = 0; i < 16; i++) {
        g_sdram_data_probe_words[i] = expected_words[i];
    }

    volatile uint8_t *probe_bytes = (volatile uint8_t *)&g_sdram_data_probe_words[0];
    for (uint32_t i = 0; i < sizeof(g_sdram_data_probe_words); i++) {
        if (probe_bytes[i] != expected[i]) {
            errors++;
        }
    }

    return errors;
}

/* Boot-time SDRAM self-test with optional status callback for OLED/log output. */
static inline bool sdramTest(sdram_status_cb_t status_cb)
{
    const uint32_t words_per_region = 1024; /* 4 KB per region */
    const uint32_t test_span_bytes = words_per_region * sizeof(uint32_t);
    const uint32_t mid_offset = SDRAM_SIZE / 2;
    const uint32_t tail_offset = SDRAM_SIZE - test_span_bytes;
    uint32_t errors = 0;

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM init...");
    sdram_wait_ready();

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T1 base");
    {
        const uint32_t step_errors = sdram_test_region(0, words_per_region, 0xA5A5A5A5UL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T1 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T2 mid ");
    {
        const uint32_t step_errors = sdram_test_region(mid_offset, words_per_region, 0x5A5A5A5AUL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T2 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T3 tail");
    {
        const uint32_t step_errors = sdram_test_region(tail_offset, words_per_region, 0x3C3CC3C3UL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T3 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T4 byte");
    {
        const uint32_t step_errors = sdram_test_byte_pattern(0x1000UL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T4 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T5 var ");
    {
        const uint32_t step_errors = sdram_test_data_section_variable();
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T5 FAIL");
    }

    if (errors == 0) {
        if (status_cb != 0) status_cb(2, "SDRAM......... OK");
        return true;
    }

    if (status_cb != 0) status_cb(2, "SDRAM....... FAIL");
    return false;
}

static inline bool test_sdram(void) {
    sdram_wait_ready();
    return sdram_test(256) == 0;
}

#endif /* SDRAM_H */
