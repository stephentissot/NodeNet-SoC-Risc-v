/**
 * sdram.h — SDRAM access helpers for Colorlight i9 SoC
 *
 * The M12L64322A SDRAM (8 MB at 0x20000000) is controlled by the LiteDRAM
 * Wishbone peripheral. After FPGA reset, the controller runs a ~200 us
 * initialization sequence before the SDRAM is accessible.
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
 *   JsonDocument json_doc(&g_sdram_json_allocator); // C++ only
 *
 * The linker assigns exact addresses automatically (starts at 0x20000000).
 * Variables are placed in order of declaration across translation units.
 *
 * NOTE: Do NOT access SDRAM_DATA variables before calling sdram_wait_ready().
 * When the application itself executes from SDRAM, destructive self-tests
 * must stay inside a reserved scratch area and must not write at SDRAM_BASE.
 */

#ifndef SDRAM_H
#define SDRAM_H

#include <stdint.h>

#ifdef __cplusplus
#include <stddef.h>
#include <cstdlib>
extern "C" {
void* malloc(size_t);
void free(void*);
void* realloc(void*, size_t);
}
#include <ArduinoJson/Memory/Allocator.hpp>
#endif

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

/* SoC status register exported by top.sv (bit0=bus stall, bit1=SDRAM init done). */
#define SOC_STATUS_ADDR            0x10000020UL
#define SOC_STATUS_BUS_STALL_BIT   (1UL << 0)
#define SOC_STATUS_SDRAM_READY_BIT (1UL << 1)

/* Dedicated destructive test area in SDRAM, reserved outside code/data use. */
#define SDRAM_TEST_SCRATCH_WORDS 4096u
extern SDRAM_DATA volatile uint32_t g_sdram_test_scratch_words[SDRAM_TEST_SCRATCH_WORDS];

#ifdef __cplusplus

/* ArduinoJson SDRAM allocator pool (default 256 KiB). */
#define SDRAM_JSON_POOL_SIZE (256UL * 1024UL)

class SdramJsonAllocator : public ArduinoJson::Allocator {
public:
    SdramJsonAllocator();

    void init(void* pool, size_t pool_size);
    bool isInitialized() const;

    void* allocate(size_t size) override;
    void deallocate(void* ptr) override;
    void* reallocate(void* ptr, size_t new_size) override;

private:
    struct BlockHeader {
        size_t size;
        BlockHeader* next;
        BlockHeader* prev;
        uint32_t used;
    };

    static constexpr uint32_t kUsedTag = 0x51A7A110u;
    static constexpr uint32_t kFreeTag = 0xFEEE0000u;

    void* pool_base_;
    size_t pool_size_;
    BlockHeader* head_;
    bool initialized_;

    static size_t alignUp(size_t value, size_t alignment);
    static BlockHeader* blockFromPayload(void* payload);
    static void* payloadFromBlock(BlockHeader* block);
    void splitBlock(BlockHeader* block, size_t wanted_size);
    void coalesce(BlockHeader* block);
};

extern SDRAM_DATA alignas(8) uint8_t g_sdram_json_pool[SDRAM_JSON_POOL_SIZE];
extern SdramJsonAllocator g_sdram_json_allocator;

/* Must be called after sdram_wait_ready() and before first JsonDocument use. */
bool sdram_json_allocator_init(void);

#endif

static inline volatile uint32_t* sdram_test_scratch_words(void)
{
    return g_sdram_test_scratch_words;
}

static inline volatile uint8_t* sdram_test_scratch_bytes(void)
{
    return (volatile uint8_t*)g_sdram_test_scratch_words;
}

/* ── Initialization wait ─────────────────────────────────────────────────── */

/**
 * sdram_wait_ready_timeout() — poll SoC status until SDRAM init is done.
 *
 * Returns true on ready, false on timeout or if the bus stall flag is latched.
 */
static inline bool sdram_wait_ready_timeout(uint32_t max_poll_loops)
{
    volatile uint32_t *status = (volatile uint32_t *)SOC_STATUS_ADDR;

    for (uint32_t i = 0; i < max_poll_loops; ++i) {
        const uint32_t v = *status;
        if ((v & SOC_STATUS_BUS_STALL_BIT) != 0UL) {
            return false;
        }
        if ((v & SOC_STATUS_SDRAM_READY_BIT) != 0UL) {
            return true;
        }
    }

    return false;
}

/**
 * sdram_wait_ready() — compatibility helper that waits with a bounded timeout,
 * then performs one scratch-area write/read probe to confirm accesses are live.
 *
 * Returns true when ready, false on timeout/stall.
 *
 * Call this once at the beginning of main(), before any SDRAM_DATA variable
 * is accessed.
 */
static inline bool sdram_wait_ready(void)
{
    if (!sdram_wait_ready_timeout(2000000UL)) {
        return false;
    }

    volatile uint32_t *p = sdram_test_scratch_words();

    /* Write a known pattern; the Wishbone stall mechanism ensures this
     * completes only once the SDRAM controller has finished its init. */
    *p = 0xA5A5A5A5UL;

    /* Dummy read to flush the pipeline. */
    (void)(*p);

    return true;
}

/**
 * sdram_test() — basic write/read-back pattern test over the reserved SDRAM
 * scratch area.
 * Returns 0 on success, or the number of mismatches found.
 *
 * Example:
 *   uint32_t errors = sdram_test(1024);  // Test first 4 KB
 */
static inline uint32_t sdram_test(uint32_t num_words)
{
    volatile uint32_t *p = sdram_test_scratch_words();
    uint32_t i, errors = 0;

    if (num_words > SDRAM_TEST_SCRATCH_WORDS) {
        num_words = SDRAM_TEST_SCRATCH_WORDS;
    }

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
    volatile uint32_t *p = (volatile uint32_t *)(sdram_test_scratch_bytes() + byte_offset);
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
    volatile uint8_t *p = sdram_test_scratch_bytes() + byte_offset;
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

/* Real section-placement probe: defined in a single translation unit. */
extern SDRAM_DATA volatile uint32_t g_sdram_data_probe_words[16];

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
    const uint32_t mid_offset = test_span_bytes;
    const uint32_t tail_offset = 2u * test_span_bytes;
    const uint32_t byte_offset = 3u * test_span_bytes;
    uint32_t errors = 0;

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM init...");
    if (!sdram_wait_ready()) {
        if (status_cb != 0) status_cb(2, "[BOOT] SDRAM timeout");
        return false;
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T1 s0  ");
    {
        const uint32_t step_errors = sdram_test_region(0, words_per_region, 0xA5A5A5A5UL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T1 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T2 s1  ");
    {
        const uint32_t step_errors = sdram_test_region(mid_offset, words_per_region, 0x5A5A5A5AUL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T2 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T3 s2  ");
    {
        const uint32_t step_errors = sdram_test_region(tail_offset, words_per_region, 0x3C3CC3C3UL);
        errors += step_errors;
        if (step_errors != 0 && status_cb != 0) status_cb(2, "[BOOT] T3 FAIL");
    }

    if (status_cb != 0) status_cb(2, "[BOOT] SDRAM T4 byte");
    {
        const uint32_t step_errors = sdram_test_byte_pattern(byte_offset);
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

static inline bool sdram_basic_test(void) {
    if (!sdram_wait_ready()) {
        return false;
    }
    return sdram_test(256) == 0;
}

#endif /* SDRAM_H */
