/**
 * led.h — MMIO driver for wb_led one-shot blink peripheral
 *
 * Register interface (single 32-bit word at base address):
 *   Write:
 *     bit0 = TRIGGER   → start a non-blocking blink pulse
 *     bit1 = SET_STATE → update the resting LED state
 *     bit2 = STATE_VAL → resting state value when bit1=1 (1=ON, 0=OFF)
 *     bits[31:3] = blink cycle count when bit0=1 (0 = use RTL default)
 *
 * Blink encoding: value = (cycles << 3) | 1
 *   where cycles = durationMs * 25000  (25 MHz clock)
 *
 * Usage:
 *   WbLed led1(0x10000008u);
 *   led1.blink(600u);
 */

#ifndef LED_H
#define LED_H

#include <stdint.h>

#define CLK_KHZ 25000u  // 25 MHz

static inline void wbLedOn(uint32_t base) { reg(base) = 0x6u; }   // bit1=SET_STATE, bit2=1
static inline void wbLedOff(uint32_t base) { reg(base) = 0x2u; }   // bit1=SET_STATE, bit2=0

// Trigger a non-blocking one-shot blink pulse.
// durationMs=0 uses the RTL default blink duration (100ms @ 25 MHz).
static inline void wbLedBlink(uint32_t base, uint32_t durationMs) {
    uint32_t cycles = durationMs * CLK_KHZ;
    reg(base) = ((cycles & 0x1FFFFFFFu) << 3) | 0x1u;
}




#endif /* LED_H */

