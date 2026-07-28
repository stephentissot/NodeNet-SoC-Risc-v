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

#define I2C_REG_STATUS   0x00u
#define I2C_REG_FIFO     0x04u
#define I2C_REG_ADDR     0x08u
#define I2C_REG_CMD      0x0Cu
#define I2C_REG_DATA     0x10u
#define I2C_REG_PRESC_LO 0x18u
#define I2C_REG_PRESC_HI 0x1Cu

#define I2C0_STATUS     (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_STATUS))
#define I2C0_FIFO       (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_FIFO))
#define I2C0_ADDR       (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_ADDR))
#define I2C0_CMD        (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_CMD))
#define I2C0_DATA       (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_DATA))
#define I2C0_PRESC_LO   (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_PRESC_LO))
#define I2C0_PRESC_HI   (*(volatile uint32_t*)(I2C0_BASE + I2C_REG_PRESC_HI))

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

#define I2C_TIMEOUT_LOOPS 1000000u

// ─── Inline helpers ──────────────────────────────────────────────────────────

class I2C {
public:
    explicit I2C(uint32_t base) : base_(base) {}

    void Init(uint16_t prescale) const {
        reg(I2C_REG_PRESC_LO) = prescale & 0xFFu;
        reg(I2C_REG_PRESC_HI) = (prescale >> 8) & 0xFFu;
    }

    bool WaitBusy() const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while (!(reg(I2C_REG_FIFO) & I2C_FIFO_CMD_EMPTY) && timeout--) {}
        if (timeout == 0) return false;

        timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_STATUS) & I2C_STATUS_BUSY) && timeout--) {}
        return timeout != 0;
    }

    bool PushData(uint8_t data) const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_WR_FULL) && timeout--) {}
        if (timeout == 0) return false;
        reg(I2C_REG_DATA) = data;
        return true;
    }

    bool PushCmd(uint8_t cmd) const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_CMD_FULL) && timeout--) {}
        if (timeout == 0) return false;
        reg(I2C_REG_CMD) = cmd;
        return true;
    }

    void SetAddress(uint8_t addr) const {
        reg(I2C_REG_ADDR) = addr;
    }

    uint16_t Prescale() const {
        return (uint16_t)(((reg(I2C_REG_PRESC_HI) & 0xFFu) << 8) |
                           (reg(I2C_REG_PRESC_LO) & 0xFFu));
    }

    int Write(uint8_t addr, const uint8_t *buf, uint8_t len) const {
        if (len == 0) return 0;

        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK; // Clear stale sticky bit
        }

        SetAddress(addr);

        if (len == 1) {
            // Single byte: START | WRITE | STOP in one command
            if (!PushData(buf[0])) return 2;
            if (!PushCmd(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
        } else {
            // First byte
            if (!PushData(buf[0])) return 2;
            if (!PushCmd(I2C_CMD_START | I2C_CMD_WRITE)) return 2;
            // Middle bytes
            for (uint8_t i = 1; i < len - 1; i++) {
                if (!PushData(buf[i])) return 2;
                if (!PushCmd(I2C_CMD_WRITE)) return 2;
            }
            // Last byte with STOP
            if (!PushData(buf[len - 1])) return 2;
            if (!PushCmd(I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
        }

        if (!WaitBusy()) return 2;

        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK; // W1C
            return 1; // NACK
        }
        return 0;
    }

    int Read(uint8_t addr, uint8_t *buf, uint8_t len) const {
        if (len == 0) return 0;

        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK; // Clear stale sticky bit
        }

        SetAddress(addr);

        // Push N read commands (last one with STOP)
        for (uint8_t i = 0; i < len; i++) {
            uint8_t cmd = (i == 0) ? (I2C_CMD_START | I2C_CMD_READ) : I2C_CMD_READ;
            if (i == len - 1) cmd |= I2C_CMD_STOP;
            if (!PushCmd(cmd)) return 2;
        }

        // Read bytes as they arrive
        for (uint8_t i = 0; i < len; i++) {
            uint32_t timeout = I2C_TIMEOUT_LOOPS;
            while ((reg(I2C_REG_FIFO) & I2C_FIFO_RD_EMPTY) && timeout--) {}
            if (timeout == 0) return 2;
            buf[i] = (uint8_t)(reg(I2C_REG_DATA) & 0xFFu);
        }

        if (!WaitBusy()) return 2;

        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK; // W1C
            return 1;
        }

        return 0;
    }

private:
    volatile uint32_t &reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t *>(base_ + offset);
    }

    uint32_t base_;
};

#endif /* I2C_H */
