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
 */

#ifndef LED_H
#define LED_H

#include <cstdint>

class WbLed {
public:
    static constexpr uint32_t CLK_KHZ = 25000u;  // 25 MHz

    explicit WbLed(uint32_t base) : base_(base) {}

    // Set LED to resting ON state
    void on()  const { reg() = 0x6u; }  // bit1=SET_STATE, bit2=1

    // Set LED to resting OFF state
    void off() const { reg() = 0x2u; }  // bit1=SET_STATE, bit2=0

    // Trigger a non-blocking one-shot blink pulse.
    // LED briefly goes to the opposite of its resting state for durationMs.
    // durationMs=0 uses the RTL default blink duration (100ms @ 25 MHz).
    void blink(uint32_t durationMs) const {
        uint32_t cycles = durationMs * CLK_KHZ;
        reg() = ((cycles & 0x1FFFFFFFu) << 3) | 0x1u;
    }

private:
    volatile uint32_t& reg() const {
        return *reinterpret_cast<volatile uint32_t*>(base_);
    }
    uint32_t base_;
};

#endif /* LED_H */

