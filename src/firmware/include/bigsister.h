/**
 * @file bigsister.h
 * @brief Small firmware utilities.
 */

#ifndef BIGSISTER_H
#define BIGSISTER_H

#include <stdint.h>

static volatile uint32_t* const TIMER_MS = reinterpret_cast<volatile uint32_t*>(0x10000010UL);

// Override this from the build if your SoC clock changes.
#ifndef BIGSISTER_CPU_HZ
#define BIGSISTER_CPU_HZ 25000000UL
#endif

// Bare-metal C++ runtime stub: called on invalid pure virtual dispatch.
extern "C" inline void __cxa_pure_virtual() { while (1); }

/**
 * Arduino-like millis(): milliseconds since boot from hardware timer.
 * Wraps naturally on uint32_t, like Arduino's unsigned long behavior.
 */
static inline uint32_t millis(void) {
    return *TIMER_MS;
}

// ─── Delay helpers ─────────────────────────────────────────────────────────
// Use the SoC's hardware timer through millis() to stay aligned with the rest
// of the bare-metal firmware instead of relying on rdcycle-based spin loops.

static void delay_ms(uint32_t ms) {
    const uint32_t start = millis();
    while ((uint32_t)(millis() - start) < ms) {
        __asm__ volatile("nop");
    }
}


#endif /* BIGSISTER_H */
