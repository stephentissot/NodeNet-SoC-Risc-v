/**
 * i2c.h — MMIO I2C driver for wb_i2c / i2c_master_wbs_8
 *
 * Pure C, static-inline functions.  The base address is passed to every
 * function so the compiler can fold a compile-time constant (e.g. 0x10005000)
 * through the call chain and emit direct MMIO addresses — zero indirection,
 * zero stack state, no constructor.
 *
 * Usage:
 *   #include "i2c.h"
 *   i2c_init(0x10005000u, 15);           // 400 kHz @ 25 MHz
 *   bool found = i2c_probe(0x10005000u, 0x3C);
 *
 * Register addresses (4-byte stride from base):
 *   +0x00  Status      R/W (W: clear sticky bits)
 *   +0x04  FIFO Status R
 *   +0x08  Cmd Addr    W   7-bit device address for next command push
 *   +0x0C  Command     W   [0]=start [1]=read [2]=write [4]=stop
 *   +0x10  Data        R/W pop RX FIFO / push TX FIFO
 *   +0x18  Prescale Lo W
 *   +0x1C  Prescale Hi W
 *
 * Protocol for a WRITE transaction of N bytes:
 *   1. Write [base + I2C_REG_ADDR] = device_addr;
 *   2. For byte 0: push DATA, then push CMD = START|WRITE
 *   3. For bytes 1..N-2: push DATA, then push CMD = WRITE
 *   4. For byte N-1: push DATA, then push CMD = WRITE|STOP
 *   5. Wait until cmd FIFO empty, then until not busy.
 *
 * Protocol for a READ transaction of N bytes:
 *   1. Write [base + I2C_REG_ADDR] = device_addr;
 *   2. Push CMD = START|READ  (for each byte to read, one cmd per byte)
 *   3. Last byte: CMD = READ|STOP
 *   4. Read N bytes from [base + I2C_REG_DATA] as they become available.
 */

#ifndef I2C_H
#define I2C_H

#include <stdint.h>

// ─── Register offsets ────────────────────────────────────────────────────────
#define I2C_REG_STATUS   0x00u
#define I2C_REG_FIFO     0x04u
#define I2C_REG_ADDR     0x08u
#define I2C_REG_CMD      0x0Cu
#define I2C_REG_DATA     0x10u
#define I2C_REG_PRESC_LO 0x18u
#define I2C_REG_PRESC_HI 0x1Cu

// ─── Status register bits ────────────────────────────────────────────────────
#define I2C_STATUS_BUSY      (1u << 0)
#define I2C_STATUS_BUS_CTRL  (1u << 1)
#define I2C_STATUS_BUS_ACT   (1u << 2)
#define I2C_STATUS_MISS_ACK  (1u << 3)   // sticky; W1C

// ─── FIFO status register bits ───────────────────────────────────────────────
#define I2C_FIFO_CMD_EMPTY   (1u << 0)
#define I2C_FIFO_CMD_FULL    (1u << 1)
#define I2C_FIFO_CMD_OVF     (1u << 2)
#define I2C_FIFO_WR_EMPTY    (1u << 3)
#define I2C_FIFO_WR_FULL     (1u << 4)
#define I2C_FIFO_WR_OVF      (1u << 5)
#define I2C_FIFO_RD_EMPTY    (1u << 6)
#define I2C_FIFO_RD_FULL     (1u << 7)

// ─── Command register bits ───────────────────────────────────────────────────
#define I2C_CMD_START    (1u << 0)
#define I2C_CMD_READ     (1u << 1)
#define I2C_CMD_WRITE    (1u << 2)
#define I2C_CMD_STOP     (1u << 4)

// ─── Timing ──────────────────────────────────────────────────────────────────
// prescale = Fclk / (FI2C × 4)  →  100 kHz @ 25 MHz = 62,  400 kHz = 15
#define I2C_CLK_HZ        25000000UL
#define I2C_TIMEOUT_LOOPS 200000u

// ─── Register accessor (volatile: every access goes to hardware) ─────────────
#define I2C_REG(base, off) \
    (*((volatile uint32_t *)((uint32_t)(base) + (uint32_t)(off))))

// ─── Primitives (all static inline: compiler folds base constant → direct addr)

static inline void i2c_init(uint32_t base, uint16_t prescale)
{
    I2C_REG(base, I2C_REG_PRESC_LO) = (uint32_t)(prescale & 0xFFu);
    I2C_REG(base, I2C_REG_PRESC_HI) = (uint32_t)((prescale >> 8) & 0xFFu);
}

static inline bool i2c_nack_detected(uint32_t base)
{
    return (I2C_REG(base, I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) != 0;
}

static inline void i2c_clear_nack(uint32_t base)
{
    I2C_REG(base, I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
}

static inline void i2c_set_address(uint32_t base, uint8_t addr)
{
    I2C_REG(base, I2C_REG_ADDR) = (uint32_t)addr;
}

static inline bool i2c_push_data(uint32_t base, uint8_t data)
{
    uint32_t timeout = I2C_TIMEOUT_LOOPS;
    while ((I2C_REG(base, I2C_REG_FIFO) & I2C_FIFO_WR_FULL) && timeout > 0) --timeout;
    if (timeout == 0) return false;
    I2C_REG(base, I2C_REG_DATA) = (uint32_t)data;
    return true;
}

static inline bool i2c_push_cmd(uint32_t base, uint8_t cmd)
{
    uint32_t timeout = I2C_TIMEOUT_LOOPS;
    while ((I2C_REG(base, I2C_REG_FIFO) & I2C_FIFO_CMD_FULL) && timeout > 0) --timeout;
    if (timeout == 0) return false;
    I2C_REG(base, I2C_REG_CMD) = (uint32_t)cmd;
    return true;
}

static inline bool i2c_wait_busy(uint32_t base)
{
    uint32_t timeout = I2C_TIMEOUT_LOOPS;
    while ((I2C_REG(base, I2C_REG_FIFO) & I2C_FIFO_CMD_EMPTY) == 0 && timeout > 0) --timeout;
    if (timeout == 0) return false;
    timeout = I2C_TIMEOUT_LOOPS;
    while ((I2C_REG(base, I2C_REG_STATUS) & I2C_STATUS_BUSY) && timeout > 0) --timeout;
    return timeout > 0;
}

// ─── High-level operations ───────────────────────────────────────────────────

// Write len bytes to 7-bit addr. Returns: 0=ACK, 1=NACK, 2=timeout.
static inline int i2c_write(uint32_t base, uint8_t addr,
                             const uint8_t *buf, uint8_t len)
{
    if (len == 0) return 0;
    if (i2c_nack_detected(base)) i2c_clear_nack(base);
    i2c_set_address(base, addr);
    if (len == 1) {
        if (!i2c_push_data(base, buf[0])) return 2;
        if (!i2c_push_cmd(base, I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
    } else {
        if (!i2c_push_data(base, buf[0])) return 2;
        if (!i2c_push_cmd(base, I2C_CMD_START | I2C_CMD_WRITE)) return 2;
        for (uint8_t i = 1; i < len; i++) {
            if (!i2c_push_data(base, buf[i])) return 2;
            if (!i2c_push_cmd(base, I2C_CMD_WRITE)) return 2;
        }
        // STOP as a standalone command — WRITE|STOP combined is unreliable on this core
        if (!i2c_push_cmd(base, I2C_CMD_STOP)) return 2;
    }
    if (!i2c_wait_busy(base)) return 2;
    if (i2c_nack_detected(base)) { i2c_clear_nack(base); return 1; }
    return 0;
}

// Read len bytes from 7-bit addr. Returns: 0=OK, 1=NACK, 2=timeout.
static inline int i2c_read(uint32_t base, uint8_t addr,
                            uint8_t *buf, uint8_t len)
{
    if (len == 0) return 0;
    if (i2c_nack_detected(base)) i2c_clear_nack(base);
    i2c_set_address(base, addr);
    for (uint8_t i = 0; i < len; i++) {
        uint8_t cmd = (i == 0) ? (I2C_CMD_START | I2C_CMD_READ) : I2C_CMD_READ;
        if (i == len - 1) cmd |= I2C_CMD_STOP;
        if (!i2c_push_cmd(base, cmd)) return 2;
    }
    for (uint8_t i = 0; i < len; i++) {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((I2C_REG(base, I2C_REG_FIFO) & I2C_FIFO_RD_EMPTY) && timeout > 0) --timeout;
        if (timeout == 0) return 2;
        buf[i] = (uint8_t)(I2C_REG(base, I2C_REG_DATA) & 0xFFu);
    }
    if (!i2c_wait_busy(base)) return 2;
    if (i2c_nack_detected(base)) { i2c_clear_nack(base); return 1; }
    return 0;
}

// Probe: returns true if device ACKs at 7-bit addr.
static inline bool i2c_probe(uint32_t base, uint8_t addr)
{
    uint8_t dummy = 0x00u;
    return i2c_write(base, addr, &dummy, 1) == 0;
}

#endif /* I2C_H */
