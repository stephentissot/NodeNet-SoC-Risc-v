#include "i2c.h"


// Private methods for low-level I2C operations


void I2c::waitNop(uint32_t timeout) {
    while (timeout > 0) {
        --timeout;
        __asm__ volatile("nop" ::: "memory");
    }
}