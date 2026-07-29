#include <cstdint>

static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);

static inline void delay_cycles(uint32_t n)
{
    while (n--) {
        __asm__ volatile ("nop");
    }
}

int main(void)
{
    // LED_D2 is active-low on this board: 0 = ON, 1 = OFF.
    static constexpr uint32_t kBlinkDelayCycles = 750000u;

    while (1) {
        *LED_D2 = 0u;
        delay_cycles(kBlinkDelayCycles);
        *LED_D2 = 1u;
        delay_cycles(kBlinkDelayCycles);
    }
}