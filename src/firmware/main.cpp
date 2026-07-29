#include <cstdint>
#include "bigsister.h"

// Harware setup
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL); // D2 LED is active-low, connected to GPIO output at 0x10000000. on the i9 board itself. The LED is wired to the GPIO output of the CPU, which is active-low. Writing 0 turns the LED on, writing 1 turns it off.

int main(void)
{
    static constexpr uint32_t kBlinkPeriodMs = 100u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;

    *LED_D2 = 1u;

    while (1) {
        // Use the hardware timer to toggle the LED every 100 ms
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
        }
    }
}