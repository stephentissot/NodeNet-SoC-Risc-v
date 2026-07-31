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
#define I2C0_BASE 0x10005000u

// ─── RISC-V cycle counter ────────────────────────────────────────────────────

static inline uint32_t rdcycle32(void) {
    uint32_t v;
    asm volatile("rdcycle %0" : "=r"(v));
    return v;
}

static void delay_ms(uint32_t ms) {
    uint32_t start = rdcycle32();
    uint32_t ticks = ms * (I2C0_CLK_HZ / 1000);
    while ((rdcycle32() - start) < ticks);
}

static void delay_us(uint32_t us) {
    uint32_t start = rdcycle32();
    uint32_t ticks = us * (I2C0_CLK_HZ / 1000000);
    while ((rdcycle32() - start) < ticks);
}

static void delay_100ns(uint32_t count) {
    // count × 100 ns
    // At 25 MHz, 100 ns = 2.5 cycles ≈ 3 cycles minimum
    // For small values, a short loop is sufficient
    uint32_t start = rdcycle32();
    uint32_t ticks = (count * I2C0_CLK_HZ) / 10000000UL;
    while ((rdcycle32() - start) < ticks);
}

// ─── I2C transfer state ──────────────────────────────────────────────────────

static uint8_t  s_i2c_addr   = 0;
static bool     s_tx_started = false;  // True after first byte is queued
static const I2C s_i2c(I2C0_BASE);

// ─── I2C byte callback ───────────────────────────────────────────────────────

extern "C" uint8_t u8x8_byte_i2c_hw(u8x8_t *u8x8, uint8_t msg,
                                     uint8_t arg_int, void *arg_ptr) {
    switch (msg) {

    case U8X8_MSG_BYTE_INIT:
        // Set I2C clock to 400 kHz for fast display updates
        s_i2c.Init((uint16_t)(I2C0_CLK_HZ / (400000UL * 4)));
        s_tx_started = false;
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        // u8g2 stores address in 8-bit form (7-bit << 1); shift back to 7-bit
        s_i2c_addr   = u8x8_GetI2CAddress(u8x8) >> 1;
        s_tx_started = false;
        s_i2c.SetAddress(s_i2c_addr);
        break;

    case U8X8_MSG_BYTE_SEND: {
        // Push bytes to I2C TX FIFO + one WRITE command per byte.
        // For the very first byte of the transfer, add START to the command.
        // The hardware stalls automatically when FIFOs are full.
        const uint8_t *data = static_cast<const uint8_t *>(arg_ptr);
        for (uint8_t i = 0; i < arg_int; i++) {
            if (!s_i2c.PushData(data[i])) {
                s_tx_started = false;
                return 0;
            }

            uint8_t cmd = I2C_CMD_WRITE;
            if (!s_tx_started) {
                cmd |= I2C_CMD_START;
                s_tx_started = true;
            }
            if (!s_i2c.PushCmd(cmd)) {
                s_tx_started = false;
                return 0;
            }
        }
        break;
    }

    case U8X8_MSG_BYTE_END_TRANSFER:
        // Issue STOP to close the I2C frame
        if (s_tx_started) {
            if (!s_i2c.PushCmd(I2C_CMD_STOP)) {
                s_tx_started = false;
                return 0;
            }
        }
        // Block until the hardware has physically finished
        if (!s_i2c.WaitBusy()) {
            s_tx_started = false;
            return 0;
        }
        // Check for NACK: device not found or not responding at this address
        if (s_i2c.NackDetected()) {
            s_i2c.ClearNack();
            s_tx_started = false;
            return 0;  // signal failure to u8g2
        }
        s_tx_started = false;
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
