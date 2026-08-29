/**
 * @file bigsister.h
 * @brief Small firmware utilities.
 */

#ifndef BIGSISTER_H
#define BIGSISTER_H

static volatile uint32_t* const TIMER_MS = reinterpret_cast<volatile uint32_t*>(0x10000010UL);

// Override this from the build if your SoC clock changes.
#ifndef BIGSISTER_CPU_HZ
#define BIGSISTER_CPU_HZ 25000000UL
#endif

// Bare-metal C++ runtime stub: called on invalid pure virtual dispatch.
extern "C" inline void __cxa_pure_virtual() { while (1); }

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL
#define MODBUS1_BASE 0x10004000u
#define FLASH_BASE 0x10007000u
#define PLC_BASE 0x10008000u
static constexpr uint32_t NODENET0_BASE = 0x10006000u;

/**
 * Arduino-like millis(): milliseconds since boot from hardware timer.
 * Wraps naturally on uint32_t, like Arduino's unsigned long behavior.
 */
[[maybe_unused]] static uint32_t millis(void) {
    return *TIMER_MS;
}

// ─── Delay helpers ─────────────────────────────────────────────────────────
// Use the SoC's hardware timer through millis() to stay aligned with the rest
// of the bare-metal firmware instead of relying on rdcycle-based spin loops.

[[maybe_unused]] static void delay(uint32_t ms) {
    uint32_t start = millis();
    while ((int32_t)(millis() - start - ms) < 0) {}
}

// Led blink helper
[[maybe_unused]] static void led_d2_blink()
{
    *LED_D2 = 1u;delay(100u);*LED_D2 = 0u;delay(100u); // led_d2 on/off
    *LED_D2 = 1u;delay(100u);*LED_D2 = 0u;delay(100u); // led_d2 on/off
    *LED_D2 = 1u;delay(100u);*LED_D2 = 0u;delay(100u); // led_d2 on/off
    *LED_D2 = 1u;delay(100u);*LED_D2 = 0u;delay(100u); // led_d2 on/off
    delay(500u);
}


#endif /* BIGSISTER_H */
