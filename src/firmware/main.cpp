#include <cstdint>

namespace {

static volatile uint32_t* const kLedD2 = reinterpret_cast<volatile uint32_t*>(0x10000000u);

static inline void raw_delay(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; ++i) {
        asm volatile("nop" ::: "memory");
    }
}

}  // namespace

extern "C" int main(void)
{
    while (true) {
        *kLedD2 = 1u;
        raw_delay(1500000u);

        *kLedD2 = 0u;
        raw_delay(1500000u);
    }
}