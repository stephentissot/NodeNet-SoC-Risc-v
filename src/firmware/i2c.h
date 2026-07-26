/**
 * i2c.h — MMIO I2C driver for wb_i2c / i2c_master_wbs_8
 *
 * Memory-mapped at I2C0_BASE (0x10005000) via Wishbone bus.
 * Wraps Alex Forencich's verilog-i2c i2c_master_wbs_8 core.
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
 *   1. I2C0_ADDR = device_addr;
 *   2. For byte 0: push DATA, then push CMD = START|WRITE
 *   3. For bytes 1..N-2: push DATA, then push CMD = WRITE
 *   4. For byte N-1: push DATA, then push CMD = WRITE|STOP
 *   5. Wait until cmd FIFO empty, then until not busy.
 *
 * Protocol for a READ transaction of N bytes:
 *   1. I2C0_ADDR = device_addr;
 *   2. Push CMD = START|READ  (for each byte to read, one cmd per byte)
 *   3. Last byte: CMD = READ|STOP
 *   4. Read N bytes from I2C0_DATA as they become available (poll rd_empty).
 */

#ifndef I2C_H
#define I2C_H

#include <stdint.h>

// ─── MMIO base and register offsets ─────────────────────────────────────────

#define I2C0_BASE       0x10005000UL

#define I2C0_STATUS     (*(volatile uint32_t*)(I2C0_BASE + 0x00))
#define I2C0_FIFO       (*(volatile uint32_t*)(I2C0_BASE + 0x04))
#define I2C0_ADDR       (*(volatile uint32_t*)(I2C0_BASE + 0x08))
#define I2C0_CMD        (*(volatile uint32_t*)(I2C0_BASE + 0x0C))
#define I2C0_DATA       (*(volatile uint32_t*)(I2C0_BASE + 0x10))
#define I2C0_PRESC_LO   (*(volatile uint32_t*)(I2C0_BASE + 0x18))
#define I2C0_PRESC_HI   (*(volatile uint32_t*)(I2C0_BASE + 0x1C))

// ─── Status register bits (0x00) ────────────────────────────────────────────
#define I2C_STATUS_BUSY      (1u << 0)  // Transaction in progress
#define I2C_STATUS_BUS_CTRL  (1u << 1)  // We hold the bus
#define I2C_STATUS_BUS_ACT   (1u << 2)  // Bus is active
#define I2C_STATUS_MISS_ACK  (1u << 3)  // Slave did not ACK (sticky; W1C)

// ─── FIFO status register bits (0x04) ───────────────────────────────────────
#define I2C_FIFO_CMD_EMPTY   (1u << 0)
#define I2C_FIFO_CMD_FULL    (1u << 1)
#define I2C_FIFO_CMD_OVF     (1u << 2)
#define I2C_FIFO_WR_EMPTY    (1u << 3)
#define I2C_FIFO_WR_FULL     (1u << 4)
#define I2C_FIFO_WR_OVF      (1u << 5)
#define I2C_FIFO_RD_EMPTY    (1u << 6)
#define I2C_FIFO_RD_FULL     (1u << 7)

// ─── Command register bits (0x0C) ───────────────────────────────────────────
#define I2C_CMD_START    (1u << 0)  // Issue (re)start condition
#define I2C_CMD_READ     (1u << 1)  // Read one byte into RX FIFO
#define I2C_CMD_WRITE    (1u << 2)  // Write one byte from TX FIFO
#define I2C_CMD_STOP     (1u << 4)  // Issue stop condition

// ─── Prescale calculation ────────────────────────────────────────────────────
// prescale = Fclk / (FI2C * 4)
// 100 kHz @ 25 MHz → 62
// 400 kHz @ 25 MHz → 15

// ─── Inline helpers ──────────────────────────────────────────────────────────

static inline void i2c0_wait_busy(void) {
    while (!(I2C0_FIFO & I2C_FIFO_CMD_EMPTY));
    while (  I2C0_STATUS & I2C_STATUS_BUSY  );
}

static inline void i2c0_push_data(uint8_t data) {
    while (I2C0_FIFO & I2C_FIFO_WR_FULL);
    I2C0_DATA = data;
}

static inline void i2c0_push_cmd(uint8_t cmd) {
    while (I2C0_FIFO & I2C_FIFO_CMD_FULL);
    I2C0_CMD = cmd;
}

/**
 * i2c0_init() — Set clock frequency.
 * @param prescale  = Fclk / (FI2C_hz * 4)
 */
static inline void i2c0_init(uint16_t prescale) {
    I2C0_PRESC_LO =  prescale       & 0xFF;
    I2C0_PRESC_HI = (prescale >> 8) & 0xFF;
}

/**
 * i2c0_write() — Blocking I2C write transaction.
 * @param addr  7-bit device address
 * @param buf   data bytes to send
 * @param len   number of bytes
 * @return 0 on success, 1 if slave NACK'd
 */
static inline int i2c0_write(uint8_t addr, const uint8_t *buf, uint8_t len) {
    if (len == 0) return 0;

    I2C0_ADDR = addr;

    if (len == 1) {
        // Single byte: START | WRITE | STOP in one command
        i2c0_push_data(buf[0]);
        i2c0_push_cmd(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP);
    } else {
        // First byte
        i2c0_push_data(buf[0]);
        i2c0_push_cmd(I2C_CMD_START | I2C_CMD_WRITE);
        // Middle bytes
        for (uint8_t i = 1; i < len - 1; i++) {
            i2c0_push_data(buf[i]);
            i2c0_push_cmd(I2C_CMD_WRITE);
        }
        // Last byte with STOP
        i2c0_push_data(buf[len - 1]);
        i2c0_push_cmd(I2C_CMD_WRITE | I2C_CMD_STOP);
    }

    i2c0_wait_busy();

    if (I2C0_STATUS & I2C_STATUS_MISS_ACK) {
        I2C0_STATUS = I2C_STATUS_MISS_ACK; // W1C
        return 1; // NACK
    }
    return 0;
}

/**
 * i2c0_read() — Blocking I2C read transaction.
 * @param addr  7-bit device address
 * @param buf   output buffer
 * @param len   number of bytes to read
 * @return 0 on success, 1 if slave NACK'd
 */
static inline int i2c0_read(uint8_t addr, uint8_t *buf, uint8_t len) {
    if (len == 0) return 0;

    I2C0_ADDR = addr;

    // Push N read commands (last one with STOP)
    for (uint8_t i = 0; i < len; i++) {
        uint8_t cmd = (i == 0) ? (I2C_CMD_START | I2C_CMD_READ) : I2C_CMD_READ;
        if (i == len - 1) cmd |= I2C_CMD_STOP;
        i2c0_push_cmd(cmd);
    }

    // Read bytes as they arrive
    for (uint8_t i = 0; i < len; i++) {
        while (I2C0_FIFO & I2C_FIFO_RD_EMPTY);
        buf[i] = (uint8_t)(I2C0_DATA & 0xFF);
    }

    i2c0_wait_busy();
    return (I2C0_STATUS & I2C_STATUS_MISS_ACK) ? 1 : 0;
}

#endif /* I2C_H */
