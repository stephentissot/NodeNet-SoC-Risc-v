#include <cstdint>

#include "led.h"

namespace {

constexpr uint32_t kLedGreenBase = 0x10000004u;
constexpr uint32_t kLedYellowBase = 0x10000008u;

static inline void raw_delay(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; ++i) {
        asm volatile("nop" ::: "memory");
    }
}

}  // namespace

extern "C" int main(void)
{
    WbLed ledGreen(kLedGreenBase);
    WbLed ledYellow(kLedYellowBase);

    ledGreen.off();
    ledYellow.off();

    while (true) {
        ledGreen.on();
        ledYellow.off();
        raw_delay(1500000u);

        ledGreen.off();
        ledYellow.on();
        raw_delay(1500000u);
    }
}