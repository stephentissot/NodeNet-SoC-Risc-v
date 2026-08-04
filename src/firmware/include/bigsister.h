/**
 * @file bigsister.h
 * @brief Small firmware utilities.
 */

#ifndef BIGSISTER_H
#define BIGSISTER_H

#include <stdint.h>
#include <stddef.h>

static volatile uint32_t * const LED_D2 = (volatile uint32_t *)(uintptr_t)0x10000000UL;
static volatile uint32_t * const TIMER_MS = (volatile uint32_t *)(uintptr_t)0x10000010UL;

// Override this from the build if your SoC clock changes.
#ifndef BIGSISTER_CPU_HZ
#define BIGSISTER_CPU_HZ 25000000UL
#endif

// Hardware definition
#define LED0_BASE  0x10000004UL // GREEN
#define LED1_BASE  0x10000008UL // YELLOW
#define I2C0_BASE  0x10005000UL


// picorv32_wb MMIO helpers
#define reg(base) (*(volatile uint32_t *)(uintptr_t)(base))
#define picorv32_write32(addr, val)   (*(volatile uint32_t *const)(addr) = (uint32_t)(val))
#define picorv32_read32(addr)         (*(volatile uint32_t *const)(addr))
#define compiler_barrier()      __asm__ volatile("" ::: "memory")
#define nop_barrier()      __asm__ volatile("nop" ::: "memory")
/**
 * Arduino-like millis(): milliseconds since boot from hardware timer.
 * Wraps naturally on uint32_t, like Arduino's unsigned long behavior.
 */
static inline uint32_t millis(void) {
    return *TIMER_MS;
}
static void delay(uint32_t ms) {
    uint32_t start = millis();
    while ((int32_t)(millis() - start - ms) < 0) {
        nop_barrier();
    }
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
