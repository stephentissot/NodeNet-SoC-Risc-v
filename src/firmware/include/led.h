/**
 * led.h - MMIO helper for wb_led one-shot blink peripherals
 *
 * Hardware behavior:
 * - Writing bit0=1 triggers a non-blocking one-shot blink pulse.
 * - bits31:3 optionally override pulse duration in clock cycles.
 * - If bits31:3 are 0, hardware uses its default RTL blink duration.
 * - bit1+bit2 command updates default LED state (On/Off).
 * - Readback: bit0=current LED level, bit1=blink active, bit2=default state.
 */

#ifndef LED_H
#define LED_H

#include <cstdint>

#define WB_LED_TRIGGER 0x1u
#define WB_LED_STATE   0x1u
#define WB_LED_BUSY    0x2u
#define WB_LED_DEFAULT 0x4u
#define WB_LED_SET_DEFAULT 0x2u
#define WB_LED_DEFAULT_VALUE 0x4u
#define WB_LED_CYCLES_PER_MS 25000u
#define WB_LED_MAX_CYCLES_FIELD 0x1fffffffu

namespace wb_led {

static inline volatile uint32_t& reg(uint32_t base) {
    return *(volatile uint32_t*)base;
}

static inline uint32_t ms_to_cycles(uint32_t durationMs) {
    uint64_t cycles = (uint64_t)durationMs * (uint64_t)WB_LED_CYCLES_PER_MS;
    if (cycles > (uint64_t)WB_LED_MAX_CYCLES_FIELD) {
        cycles = (uint64_t)WB_LED_MAX_CYCLES_FIELD;
    }
    return (uint32_t)cycles;
}

static inline uint32_t make_blink_cmd(uint32_t cycles) {
    return ((cycles & WB_LED_MAX_CYCLES_FIELD) << 3) | WB_LED_TRIGGER;
}

static inline uint32_t make_default_cmd(bool defaultOn) {
    return WB_LED_SET_DEFAULT | (defaultOn ? WB_LED_DEFAULT_VALUE : 0u);
}

static inline void trigger(uint32_t base) {
    reg(base) = WB_LED_TRIGGER;
}

static inline void trigger_ms(uint32_t base, uint32_t durationMs) {
    reg(base) = make_blink_cmd(ms_to_cycles(durationMs));
}

static inline bool busy(uint32_t base) {
    return (reg(base) & WB_LED_BUSY) != 0;
}

static inline bool state(uint32_t base) {
    return (reg(base) & WB_LED_STATE) != 0;
}

static inline bool default_state(uint32_t base) {
    return (reg(base) & WB_LED_DEFAULT) != 0;
}

static inline void set_default_state(uint32_t base, bool defaultOn) {
    reg(base) = make_default_cmd(defaultOn);
}

// Non-blocking blink request on selected LED.
// durationMs=0 keeps hardware default pulse duration.
static inline void blink(uint32_t base, uint32_t durationMs) {
    trigger_ms(base, durationMs);
}

class Led {
public:
    explicit Led(uint32_t base, bool defaultOn = false)
        : base_(base) {
        set_default_state(base_, defaultOn);
    }

    void On() {
        set_default_state(base_, true);
    }

    void Off() {
        set_default_state(base_, false);
    }

    void Blink(uint32_t durationMs) {
        blink(base_, durationMs);
    }

    bool IsOn() const {
        return state(base_);
    }

    bool IsBusy() const {
        return busy(base_);
    }

    bool DefaultOn() const {
        return default_state(base_);
    }

private:
    uint32_t base_;
};

}  // namespace wb_led

#endif  // LED_H
