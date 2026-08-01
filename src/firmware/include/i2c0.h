#ifndef I2C0_H
#define I2C0_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bare-metal Wishbone I2C driver for wb_i2c.sv.
 *
 * The driver is written for the Alex Forencich i2c_master_wbs_8 core
 * exposed behind the wb_i2c wrapper. The peripheral is memory-mapped at
 * 0x10005000 and exposed as 32-bit MMIO registers with a 4-byte word stride.
 *
 * Safety with optimization:
 *   - every MMIO access is done through volatile pointers,
 *   - compiler barriers are inserted around each read/write,
 *   - polling loops never assume the compiler will re-read the register.
 */

#ifndef I2C0_BASE
#define I2C0_BASE 0x10005000u
#endif

#ifndef I2C0_TIMEOUT_LOOP
#define I2C0_TIMEOUT_LOOP 2000000u
#endif

/* Register offsets (4-byte stride from base). */
#define I2C0_REG_STATUS      0x00u
#define I2C0_REG_FIFO        0x04u
#define I2C0_REG_ADDR        0x08u
#define I2C0_REG_CMD         0x0Cu
#define I2C0_REG_DATA        0x10u
#define I2C0_REG_PRESC_LO    0x18u
#define I2C0_REG_PRESC_HI    0x1Cu

/* Status bits. */
#define I2C0_STATUS_BUSY      (1u << 0)
#define I2C0_STATUS_BUS_CTRL  (1u << 1)
#define I2C0_STATUS_BUS_ACT   (1u << 2)
#define I2C0_STATUS_MISS_ACK  (1u << 3)

/* FIFO status bits. */
#define I2C0_FIFO_CMD_EMPTY   (1u << 0)
#define I2C0_FIFO_CMD_FULL    (1u << 1)
#define I2C0_FIFO_CMD_OVF     (1u << 2)
#define I2C0_FIFO_WR_EMPTY    (1u << 3)
#define I2C0_FIFO_WR_FULL     (1u << 4)
#define I2C0_FIFO_WR_OVF      (1u << 5)
#define I2C0_FIFO_RD_EMPTY    (1u << 6)
#define I2C0_FIFO_RD_FULL     (1u << 7)

/* Command bits. */
#define I2C0_CMD_START        (1u << 0)
#define I2C0_CMD_READ         (1u << 1)
#define I2C0_CMD_WRITE        (1u << 2)
#define I2C0_CMD_STOP         (1u << 4)

enum i2c0_result {
    I2C0_OK = 0,
    I2C0_NACK = 1,
    I2C0_TIMEOUT = 2,
    I2C0_FIFO_ERROR = 3
};

static inline uintptr_t i2c0_reg_addr(uint32_t reg)
{
    return (uintptr_t)I2C0_BASE + (uintptr_t)reg;
}

static inline uint32_t i2c0_reg_read32(uint32_t reg)
{
    volatile uint32_t *addr = (volatile uint32_t *)i2c0_reg_addr(reg);
    uint32_t value = *addr;
    __asm__ volatile("" ::: "memory");
    return value;
}

static inline void i2c0_reg_write32(uint32_t reg, uint32_t value)
{
    volatile uint32_t *addr = (volatile uint32_t *)i2c0_reg_addr(reg);
    *addr = value;
    __asm__ volatile("" ::: "memory");
}

static inline bool i2c0_nack_detected(void)
{
    return (i2c0_reg_read32(I2C0_REG_STATUS) & I2C0_STATUS_MISS_ACK) != 0u;
}

static inline void i2c0_clear_nack(void)
{
    i2c0_reg_write32(I2C0_REG_STATUS, I2C0_STATUS_MISS_ACK);
}

static inline bool i2c0_wait_idle(uint32_t timeout)
{
    uint32_t t = timeout != 0u ? timeout : I2C0_TIMEOUT_LOOP;
    while ((i2c0_reg_read32(I2C0_REG_FIFO) & I2C0_FIFO_CMD_EMPTY) == 0u && t > 0u) {
        --t;
    }
    if (t == 0u) {
        return false;
    }

    t = timeout != 0u ? timeout : I2C0_TIMEOUT_LOOP;
    while ((i2c0_reg_read32(I2C0_REG_STATUS) & I2C0_STATUS_BUSY) != 0u && t > 0u) {
        --t;
    }
    return t > 0u;
}

static inline bool i2c0_push_data(uint8_t data)
{
    uint32_t timeout = I2C0_TIMEOUT_LOOP;
    while ((i2c0_reg_read32(I2C0_REG_FIFO) & I2C0_FIFO_WR_FULL) != 0u && timeout > 0u) {
        --timeout;
    }
    if (timeout == 0u) {
        return false;
    }
    i2c0_reg_write32(I2C0_REG_DATA, (uint32_t)data);
    return true;
}

static inline bool i2c0_push_cmd(uint8_t cmd)
{
    uint32_t timeout = I2C0_TIMEOUT_LOOP;
    while ((i2c0_reg_read32(I2C0_REG_FIFO) & I2C0_FIFO_CMD_FULL) != 0u && timeout > 0u) {
        --timeout;
    }
    if (timeout == 0u) {
        return false;
    }
    i2c0_reg_write32(I2C0_REG_CMD, (uint32_t)cmd);
    return true;
}

static inline void i2c0_init(uint16_t prescale)
{
    i2c0_reg_write32(I2C0_REG_PRESC_LO, (uint32_t)(prescale & 0xFFu));
    i2c0_reg_write32(I2C0_REG_PRESC_HI, (uint32_t)((prescale >> 8) & 0xFFu));
    i2c0_clear_nack();
    i2c0_reg_write32(I2C0_REG_CMD, I2C0_CMD_STOP);
    (void)i2c0_wait_idle(I2C0_TIMEOUT_LOOP);
}

static inline void i2c0_set_address(uint8_t addr)
{
    i2c0_reg_write32(I2C0_REG_ADDR, (uint32_t)addr);
}

static inline int i2c0_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C0_OK;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
    }

    i2c0_set_address(addr);

    if (len == 1u) {
        if (!i2c0_push_data(buf[0])) {
            return I2C0_FIFO_ERROR;
        }
        if (!i2c0_push_cmd(I2C0_CMD_START | I2C0_CMD_WRITE | I2C0_CMD_STOP)) {
            return I2C0_FIFO_ERROR;
        }
    } else {
        if (!i2c0_push_data(buf[0])) {
            return I2C0_FIFO_ERROR;
        }
        if (!i2c0_push_cmd(I2C0_CMD_START | I2C0_CMD_WRITE)) {
            return I2C0_FIFO_ERROR;
        }

        for (size_t i = 1; i + 1u < len; ++i) {
            if (!i2c0_push_data(buf[i])) {
                return I2C0_FIFO_ERROR;
            }
            if (!i2c0_push_cmd(I2C0_CMD_WRITE)) {
                return I2C0_FIFO_ERROR;
            }
        }

        if (!i2c0_push_data(buf[len - 1u])) {
            return I2C0_FIFO_ERROR;
        }
        if (!i2c0_push_cmd(I2C0_CMD_WRITE | I2C0_CMD_STOP)) {
            return I2C0_FIFO_ERROR;
        }
    }

    if (!i2c0_wait_idle(I2C0_TIMEOUT_LOOP)) {
        return I2C0_TIMEOUT;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
        return I2C0_NACK;
    }

    return I2C0_OK;
}

static inline int i2c0_read(uint8_t addr, uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C0_OK;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
    }

    i2c0_set_address(addr);

    for (size_t i = 0; i < len; ++i) {
        uint8_t cmd = (i == 0u) ? (I2C0_CMD_START | I2C0_CMD_READ) : I2C0_CMD_READ;
        if (i + 1u == len) {
            cmd |= I2C0_CMD_STOP;
        }
        if (!i2c0_push_cmd(cmd)) {
            return I2C0_FIFO_ERROR;
        }
    }

    for (size_t i = 0; i < len; ++i) {
        uint32_t timeout = I2C0_TIMEOUT_LOOP;
        while ((i2c0_reg_read32(I2C0_REG_FIFO) & I2C0_FIFO_RD_EMPTY) != 0u && timeout > 0u) {
            --timeout;
        }
        if (timeout == 0u) {
            return I2C0_TIMEOUT;
        }
        buf[i] = (uint8_t)(i2c0_reg_read32(I2C0_REG_DATA) & 0xFFu);
    }

    if (!i2c0_wait_idle(I2C0_TIMEOUT_LOOP)) {
        return I2C0_TIMEOUT;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
        return I2C0_NACK;
    }

    return I2C0_OK;
}

static inline bool i2c0_probe(uint8_t addr)
{
    uint8_t dummy = 0u;
    for (int attempt = 0; attempt < 3; ++attempt) {
        i2c0_clear_nack();
        i2c0_reg_write32(I2C0_REG_CMD, I2C0_CMD_STOP);
        (void)i2c0_wait_idle(I2C0_TIMEOUT_LOOP);
        if (i2c0_write(addr, &dummy, 1u) == I2C0_OK) {
            return true;
        }
    }
    return false;
}

/* Compatibility aliases used by the existing firmware code. */
// static inline void i2c_init(uint16_t prescale) { i2c0_init(prescale); }
// static inline bool i2c_nack_detected(void) { return i2c0_nack_detected(); }
// static inline void i2c_clear_nack(void) { i2c0_clear_nack(); }
// static inline void i2c_set_address(uint8_t addr) { i2c0_set_address(addr); }
// static inline int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len) { return i2c0_write(addr, buf, (size_t)len); }
// static inline int i2c_read(uint8_t addr, uint8_t *buf, uint8_t len) { return i2c0_read(addr, buf, (size_t)len); }
// static inline bool i2c_probe(uint8_t addr) { return i2c0_probe(addr); }

#endif /* I2C0_H */
