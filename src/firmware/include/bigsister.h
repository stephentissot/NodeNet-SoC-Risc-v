/**
 * @file bigsister.h
 * @brief Small firmware utilities.
 */

#ifndef BIGSISTER_H
#define BIGSISTER_H

#include <stdint.h>

// Override this from the build if your SoC clock changes.
#ifndef BIGSISTER_CPU_HZ
#define BIGSISTER_CPU_HZ 25000000UL
#endif

static inline uint64_t bigsister_read_mcycle(void) {
#if defined(__INTELLISENSE__)
    return 0;
#else
    uint32_t hi0;
    uint32_t lo;
    uint32_t hi1;

    do {
        asm volatile ("csrr %0, mcycleh" : "=r"(hi0));
        asm volatile ("csrr %0, mcycle"  : "=r"(lo));
        asm volatile ("csrr %0, mcycleh" : "=r"(hi1));
    } while (hi0 != hi1);

    return ((uint64_t)hi0 << 32) | lo;
#endif
}

/**
 * Arduino-like millis(): milliseconds since boot.
 * Wraps naturally on uint32_t, like Arduino's unsigned long behavior.
 */
static inline uint32_t millis(void) {
    return (uint32_t)((bigsister_read_mcycle() * 1000ULL) / (uint64_t)BIGSISTER_CPU_HZ);
}

#endif /* BIGSISTER_H */
