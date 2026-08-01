/**
 * memoryLab.cpp — Toolchain / startup chain validation firmware
 *
 * Systematically tests every category of memory that the C++ toolchain,
 * linker script and startup code (start.S) must handle correctly.
 * Any failure here means the binary is broken BEFORE we even look at
 * application bugs.
 *
 * ┌──────┬──────────────────────────────────────────────────────────────────┐
 * │ idx  │ What is validated                                                │
 * ├──────┼──────────────────────────────────────────────────────────────────┤
 * │  0   │ A0 — large uint32_t in .data          (ROM→RAM copy, start.S)   │
 * │  1   │ A1 — uint16_t + uint8_t (.data/.sdata) same mechanism           │
 * │  2   │ A2 — signed int32_t in .data                                    │
 * │  3   │ A3 — uint32_t[4] array in .data                                 │
 * │  4   │ A4 — struct with 3 fields in .data                              │
 * │  5   │ B0 — uint32_t zero-init → .bss        (BSS zeroing, start.S)   │
 * │  6   │ B1 — uint8_t zero-init → .sbss        (gp-relative, link.ld)   │
 * │  7   │ B2 — uint8_t[64] buffer → .bss all zeros                       │
 * │  8   │ B3 — struct zero-init → .bss all zeros                         │
 * │  9   │ C0 — const uint32_t/uint8_t → .rodata (ROM, no copy needed)    │
 * │ 10   │ D0 — C++ global constructor → .init_array called before main() │
 * │ 11   │ E0 — local uint32_t stack variables                             │
 * │ 12   │ E1 — local uint8_t[8] array on stack                           │
 * │ 13   │ F0 — I2C class: local ctor + MMIO write (no crash = pass)       │
 * └──────┴──────────────────────────────────────────────────────────────────┘
 *
 * Results are in volatile gResults[14] — inspect in RAM after execution:
 *   0x01 = PASS,  0xFF = FAIL,  0x00 = not reached
 * gPassCount / gFailCount give totals.
 * gPassCount bit31 = 1 means I2C probe found OLED at 0x3C (bonus info).
 *
 * LED feedback (after all tests):
 *   LED1 100 ms flash every  400 ms → ALL 14 TESTS PASS
 *   LED1 500 ms flash every 2000 ms → ≥1 FAILURE  (check gResults[])
 *   LED_D2 toggles every 2 s        → heartbeat (main loop alive)
 *
 * Build:   make lab          (produces build/lab.{hex,lst,map})
 * Program: make lab-fw       (patches FPGA BRAM via ecpbram, then programs)
 */

#include <cstdint>
#include "bigsister.h"   // millis(), TIMER_MS
#include "led.h"         // WbLed class
#include "i2c.h"         // I2C class (validates class in local scope)

// ── Hardware addresses ────────────────────────────────────────────────────────
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000u

// ═════════════════════════════════════════════════════════════════════════════
// A — Initialized globals  (.data / .sdata)
//     These live in ROM at link time (LMA) and must be copied to RAM (VMA)
//     by the startup copy loop in start.S before main() runs.
// ═════════════════════════════════════════════════════════════════════════════

// volatile: prevents GCC -Os from constant-folding the reads away and
// from discarding the sections via --gc-sections. Without volatile these
// variables are removed from the binary because GCC knows their values
// at compile time and never emits actual load instructions.
static volatile uint32_t  gA_u32    = 0xDEADBEEFu;
static volatile uint16_t  gA_u16    = 0xBEEFu;
static volatile uint8_t   gA_u8     = 0x42u;   // GCC -Os may place in .sdata
static volatile int32_t   gA_i32    = -1234567;

static volatile uint32_t  gA_arr[4] = { 0x11111111u, 0x22222222u,
                                        0x33333333u, 0x44444444u };

struct TData { uint32_t magic; uint8_t tag; uint32_t checksum; };
static volatile TData gA_struct = { 0xCAFEBABEu, 0xA5u, 0x5A5A5A5Au };

// ═════════════════════════════════════════════════════════════════════════════
// B — Zero-initialized globals  (.bss / .sbss)
//     GCC places small zero-inits in .sbss (gp-relative access).
//     BOTH sections must be cleared by the BSS zeroing loop in start.S,
//     and BOTH must be covered by link.ld's .bss section.
// ═════════════════════════════════════════════════════════════════════════════

static volatile uint32_t  gB_u32;           // .bss
static volatile uint8_t   gB_u8;            // .sbss  ← THE critical one that was missing
static volatile uint8_t   gB_buf[64];       // .bss
static volatile TData     gB_struct;        // .bss

// ═════════════════════════════════════════════════════════════════════════════
// C — Constants (.rodata)
//     Placed directly in ROM; the CPU reads them there.  No copy needed.
//     Verify their values are correct (ensures ROM is programmed correctly).
// ═════════════════════════════════════════════════════════════════════════════

static const volatile uint32_t gC_magic = 0x55AA55AAu;
static const volatile uint8_t  gC_byte  = 0xC3u;

// ═════════════════════════════════════════════════════════════════════════════
// D — C++ global with constructor
//     The compiler emits a pointer to Canary::Canary() in .init_array.
//     start.S must iterate .init_array and call each function pointer BEFORE
//     calling main().  If it doesn't, gD_canary.sentinel_ == garbage ≠ 0xC0FFEE.
// ═════════════════════════════════════════════════════════════════════════════

class Canary {
public:
    explicit Canary(uint32_t seed)
        : value_(seed ^ 0xA5A5A5A5u)
        , sentinel_(0xC0FFEEu) {}

    bool ok()      const { return sentinel_ == 0xC0FFEEu; }
    uint32_t val() const { return value_; }

private:
    uint32_t value_;
    uint32_t sentinel_;
};

static Canary gD_canary(0x12345678u);   // needs .init_array call

// ═════════════════════════════════════════════════════════════════════════════
// Test infrastructure  (volatile: GCC cannot eliminate reads/writes)
// ═════════════════════════════════════════════════════════════════════════════

#define NUM_TESTS 14

static volatile uint8_t  gResults[NUM_TESTS]; // 0=not run  0x01=pass  0xFF=fail
static volatile uint32_t gPassCount;           // total passing tests
static volatile uint32_t gFailCount;           // total failing tests (0 = all good)

static void record(uint8_t idx, bool ok) {
    gResults[idx] = ok ? 0x01u : 0xFFu;
    if (ok) { ++gPassCount; } else { ++gFailCount; }
}

// ═════════════════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════════════════

int main(void)
{
    WbLed led1(LED1_BASE);
    *LED_D2 = 1u;   // D2 ON: firmware reached main()

    // ── A: Initialized globals ─────────────────────────────────────────────
    record( 0,  gA_u32 == 0xDEADBEEFu);
    record( 1,  gA_u16 == 0xBEEFu && gA_u8 == 0x42u);
    record( 2,  gA_i32 == -1234567);
    record( 3,  gA_arr[0] == 0x11111111u && gA_arr[1] == 0x22222222u &&
                gA_arr[2] == 0x33333333u && gA_arr[3] == 0x44444444u);
    {
        uint32_t m = gA_struct.magic, c = gA_struct.checksum;
        uint8_t  t = gA_struct.tag;
        record( 4, m == 0xCAFEBABEu && t == 0xA5u && c == 0x5A5A5A5Au);
    }

    // ── B: Zero-initialized globals ────────────────────────────────────────
    record( 5,  gB_u32 == 0u);
    record( 6,  gB_u8  == 0u);         // .sbss — fails if link.ld/.bss missing *(.sbss*)
    {
        bool ok = true;
        for (int i = 0; i < 64; i++) if (gB_buf[i] != 0u) { ok = false; break; }
        record(7, ok);
    }
    {
        uint32_t m = gB_struct.magic, c = gB_struct.checksum;
        uint8_t  t = gB_struct.tag;
        record( 8, m == 0u && t == 0u && c == 0u);
    }

    // ── C: Constants ────────────────────────────────────────────────────────
    record( 9,  gC_magic == 0x55AA55AAu && gC_byte == 0xC3u);

    // ── D: C++ global constructor ───────────────────────────────────────────
    record(10,  gD_canary.ok());       // fails if .init_array not called

    // ── E: Stack / local variables ──────────────────────────────────────────
    {
        volatile uint32_t loc_a = 0xABCD1234u;
        volatile uint32_t loc_b = loc_a + 1u;
        record(11, loc_a == 0xABCD1234u && loc_b == 0xABCD1235u);
    }
    {
        volatile uint8_t loc_arr[8] = {0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80};
        bool ok = true;
        for (int i = 0; i < 8; i++)
            if (loc_arr[i] != static_cast<uint8_t>((i + 1) << 4)) { ok = false; break; }
        record(12, ok);
    }

    // ── F: I2C class in local scope ─────────────────────────────────────────
    {
        I2C i2c(I2C0_BASE);
        i2c.begin();                      // writes prescale to MMIO — hang/crash = broken
        bool probe = i2c.Probe(0x3C);     // true if OLED present (informational)
        record(13, true);                 // reaching here without hang = PASS
        if (probe) gPassCount |= 0x80000000u;  // bit31 = OLED found at 0x3C
    }

    // ── Result blink loop ───────────────────────────────────────────────────
    bool     all_pass  = (gFailCount == 0);
    uint32_t blink_ms  = all_pass ? 100u  : 500u;
    uint32_t period_ms = all_pass ? 400u  : 2000u;
    uint32_t next_led  = millis();
    uint32_t next_d2   = millis() + 2000u;
    bool     d2_on     = true;

    while (1) {
        uint32_t now = millis();

        if ((int32_t)(now - next_led) >= 0) {
            led1.blink(blink_ms);
            next_led += period_ms;
        }
        if ((int32_t)(now - next_d2) >= 0) {
            d2_on = !d2_on;
            *LED_D2 = d2_on ? 1u : 0u;
            next_d2 += 2000u;
        }
    }
}
