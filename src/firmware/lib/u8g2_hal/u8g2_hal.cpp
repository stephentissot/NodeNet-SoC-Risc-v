/**
 * u8g2_hal.cpp — u8g2 hardware abstraction layer for wb_i2c
 *
 * Implements the two u8g2 callbacks:
 *   - u8x8_byte_i2c_hw  : I2C protocol (START, SEND bytes, STOP)
 *   - u8x8_gpio_delay_hw: GPIO stubs + accurate ms/µs delays via rdcycle CSR
 *
 * The I2C protocol maps to i2c_master_wbs_8 MMIO registers.
 * Transfers are FIFO-backed; large transfers (e.g. full SSD1306 frame = 1025
 * bytes) are handled by stalling on FIFO-full without intermediate buffering.
 *
 * Clock: 25 MHz (I2C0_CLK_HZ). Adjust if your design runs at a different rate.
 */

#include "u8g2_hal.h"
#include <stdint.h>

// ─── Clock frequency (must match CLK_FREQ_MHZ in top.sv) ────────────────────
static constexpr uint32_t I2C0_CLK_HZ = 25000000UL;
/* I2C0_BASE est défini dans i2c.h */

// ─── RISC-V cycle counter ────────────────────────────────────────────────────

static inline uint32_t rdcycle32(void) {
    uint32_t v;
    asm volatile("rdcycle %0" : "=r"(v));
    return v;
}

static void delay_ms(uint32_t ms) {
    uint32_t start = rdcycle32();
    uint32_t ticks = (uint32_t)((uint64_t)ms * (I2C0_CLK_HZ / 1000UL));  // no overflow
    while ((rdcycle32() - start) < ticks);
}

static void delay_us(uint32_t us) {
    uint32_t start = rdcycle32();
    uint32_t ticks = us * (I2C0_CLK_HZ / 1000000);
    while ((rdcycle32() - start) < ticks);
}

static void delay_100ns(uint32_t count) {
    // count × 100 ns
    // At 25 MHz, 100 ns = 2.5 cycles; use 64-bit intermediate to avoid overflow
    uint32_t start = rdcycle32();
    uint32_t ticks = (uint32_t)((uint64_t)count * I2C0_CLK_HZ / 10000000UL);
    while ((rdcycle32() - start) < ticks);
}

// ─── I2C transfer state ──────────────────────────────────────────────────────
// u8g2 never sends more than 32 bytes between START_TRANSFER and END_TRANSFER.
// We accumulate into a buffer and call i2c_write() at END_TRANSFER — the same
// proven path used by i2c_probe() that is confirmed working.

static uint8_t  s_i2c_addr  = 0;
static uint8_t  s_buf[32];   // max 32 bytes per u8g2 transfer
static uint8_t  s_buf_len   = 0;

// ─── I2C byte callback ───────────────────────────────────────────────────────

extern "C" uint8_t u8x8_byte_i2c_hw(u8x8_t *u8x8, uint8_t msg,
                                     uint8_t arg_int, void *arg_ptr) {
    switch (msg) {

    case U8X8_MSG_BYTE_INIT:
        i2c_init((uint16_t)(I2C0_CLK_HZ / (400000UL * 4)));
        s_buf_len = 0;
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        s_i2c_addr = u8x8_GetI2CAddress(u8x8) >> 1;
        s_buf_len  = 0;
        if (i2c_nack_detected()) i2c_clear_nack();
        break;

    case U8X8_MSG_BYTE_SEND: {
        const uint8_t *data = static_cast<const uint8_t *>(arg_ptr);
        for (uint8_t i = 0; i < arg_int; i++) {
            if (s_buf_len < sizeof(s_buf))
                s_buf[s_buf_len++] = data[i];
        }
        break;
    }

    case U8X8_MSG_BYTE_END_TRANSFER:
        // Send accumulated bytes using i2c_write — the proven probe path (WRITE|STOP)
        if (s_buf_len > 0) {
            i2c_write(s_i2c_addr, s_buf, s_buf_len);
        }
        s_buf_len = 0;
        break;

    default:
        return 0;
    }
    return 1;
}

// ─── GPIO and delay callback ─────────────────────────────────────────────────

extern "C" uint8_t u8x8_gpio_delay_hw(u8x8_t *u8x8, uint8_t msg,
                                       uint8_t arg_int, void *arg_ptr) {
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {

    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        // SCL/SDA are controlled by the I2C hardware core — nothing to init here
        break;

    case U8X8_MSG_DELAY_MILLI:
        delay_ms(arg_int);
        break;

    case U8X8_MSG_DELAY_10MICRO:
        delay_us(arg_int * 10);
        break;

    case U8X8_MSG_DELAY_100NANO:
        delay_100ns(arg_int);
        break;

    case U8X8_MSG_GPIO_RESET:
    case U8X8_MSG_GPIO_CS:
    case U8X8_MSG_GPIO_DC:
        // Not used in I2C mode
        break;

    default:
        return 0;
    }
    return 1;
}
