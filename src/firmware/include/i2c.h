/**
 * i2c.h — MMIO I2C driver for wb_i2c / i2c_master_wbs_8
 *
 * Memory-mapped via a caller-provided base address (for example 0x10005000).
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

// ─── MMIO register offsets ───────────────────────────────────────────────────

#define I2C_REG_STATUS   0x00u
#define I2C_REG_FIFO     0x04u
#define I2C_REG_ADDR     0x08u
#define I2C_REG_CMD      0x0Cu
#define I2C_REG_DATA     0x10u
#define I2C_REG_PRESC_LO 0x18u
#define I2C_REG_PRESC_HI 0x1Cu

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

#define I2C_TIMEOUT_LOOPS 200000u

// ─── Buffer sizes ─────────────────────────────────────────────────────────────
#define I2C_TX_BUFFER_SIZE 32u
#define I2C_RX_BUFFER_SIZE 32u

// ─── I2C class ────────────────────────────────────────────────────────────────
// Base address passed at construction time.
// All low-level MMIO methods are inline so GCC -Os never generates
// incorrect out-of-line member-array accesses via the `this` pointer.
// Wire-compatible API (begin … flush) is declared here and implemented
// in i2c.cpp so that endTransmission calls the inline Write() correctly.
//
// Usage:
//   I2C i2c0(0x10005000u);    // local or global — constructor sets base_
//   i2c0.begin();             // 400 kHz @ 25 MHz
//   i2c0.beginTransmission(0x3C);
//   i2c0.write(0x00);
//   uint8_t err = i2c0.endTransmission();  // 0 = ACK

class I2C {
public:
    static constexpr uint32_t CLK_HZ = 25000000UL;

    explicit I2C(uint32_t base)
        : base_(base), tx_address_(0), tx_length_(0),
          rx_length_(0), rx_position_(0) {}

    // ── Low-level MMIO (all inline — prevents GCC -Os out-of-line miscompile) ─

    [[gnu::always_inline]] inline void Init(uint16_t prescale) const {
        reg(I2C_REG_PRESC_LO) = prescale & 0xFFu;
        reg(I2C_REG_PRESC_HI) = (prescale >> 8) & 0xFFu;
    }

    [[gnu::always_inline]] inline bool WaitBusy() const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_CMD_EMPTY) == 0 && timeout > 0) { --timeout; }
        if (timeout == 0) return false;
        timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_STATUS) & I2C_STATUS_BUSY) && timeout > 0) { --timeout; }
        return timeout > 0;
    }

    [[gnu::always_inline]] inline bool NackDetected() const {
        return (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) != 0;
    }

    [[gnu::always_inline]] inline void ClearNack() const {
        reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
    }

    [[gnu::always_inline]] inline bool PushData(uint8_t data) const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_WR_FULL) && timeout > 0) { --timeout; }
        if (timeout == 0) return false;
        reg(I2C_REG_DATA) = data;
        return true;
    }

    [[gnu::always_inline]] inline bool PushCmd(uint8_t cmd) const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_CMD_FULL) && timeout > 0) { --timeout; }
        if (timeout == 0) return false;
        reg(I2C_REG_CMD) = cmd;
        return true;
    }

    [[gnu::always_inline]] inline void SetAddress(uint8_t addr) const {
        reg(I2C_REG_ADDR) = addr;
    }

    [[gnu::always_inline]] inline bool Probe(uint8_t addr7bit) const {
        uint8_t dummy = 0x00u;
        return Write(addr7bit, &dummy, 1) == 0;
    }

    [[gnu::always_inline]] inline int Write(uint8_t addr, const uint8_t *buf, uint8_t len) const {
        if (len == 0) return 0;
        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK)
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
        SetAddress(addr);
        if (len == 1) {
            if (!PushData(buf[0])) return 2;
            if (!PushCmd(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
        } else {
            if (!PushData(buf[0])) return 2;
            if (!PushCmd(I2C_CMD_START | I2C_CMD_WRITE)) return 2;
            for (uint8_t i = 1; i < len - 1; i++) {
                if (!PushData(buf[i])) return 2;
                if (!PushCmd(I2C_CMD_WRITE)) return 2;
            }
            if (!PushData(buf[len - 1])) return 2;
            if (!PushCmd(I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
        }
        if (!WaitBusy()) return 2;
        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
            return 1;
        }
        return 0;
    }

    [[gnu::always_inline]] inline int Read(uint8_t addr, uint8_t *buf, uint8_t len) const {
        if (len == 0) return 0;
        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK)
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
        SetAddress(addr);
        for (uint8_t i = 0; i < len; i++) {
            uint8_t cmd = (i == 0) ? (I2C_CMD_START | I2C_CMD_READ) : I2C_CMD_READ;
            if (i == len - 1) cmd |= I2C_CMD_STOP;
            if (!PushCmd(cmd)) return 2;
        }
        for (uint8_t i = 0; i < len; i++) {
            uint32_t timeout = I2C_TIMEOUT_LOOPS;
            while ((reg(I2C_REG_FIFO) & I2C_FIFO_RD_EMPTY) && timeout > 0) { --timeout; }
            if (timeout == 0) return 2;
            buf[i] = (uint8_t)(reg(I2C_REG_DATA) & 0xFFu);
        }
        if (!WaitBusy()) return 2;
        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
            return 1;
        }
        return 0;
    }

    // ── Arduino Wire-compatible API (implemented in i2c.cpp) ─────────────────
    void    begin();
    void    setClock(uint32_t freq);
    void    beginTransmission(uint8_t addr);
    uint8_t write(uint8_t value);
    uint8_t endTransmission(bool sendStop = true);
    uint8_t requestFrom(uint8_t addr, uint8_t qty, bool stop = true);
    int     available() const;
    int     read();
    int     peek() const;
    void    flush();

private:
    [[gnu::always_inline]] inline volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
    }

    uint32_t base_;
    uint8_t  tx_address_;
    uint8_t  tx_buffer_[I2C_TX_BUFFER_SIZE];
    uint8_t  tx_length_;
    uint8_t  rx_buffer_[I2C_RX_BUFFER_SIZE];
    uint8_t  rx_length_;
    uint8_t  rx_position_;
};

#endif /* I2C_H */
