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

// ─── I2C class — BASE address as template parameter ──────────────────────────
// Address baked in at compile-time: no constructor, no .sbss, global-scope safe.
// Wire-compatible API (begin/beginTransmission/write/endTransmission/requestFrom/
// read/available/peek/flush) is included — all methods are implicitly inline
// (template), so GCC -Os cannot generate incorrect out-of-line member accesses.
//
// Usage:
//   I2C<0x10005000u> i2c0;            // global scope — works correctly
//   i2c0.begin();                     // 400 kHz @ 25 MHz
//   i2c0.beginTransmission(0x3C);
//   i2c0.write(0x00);
//   uint8_t err = i2c0.endTransmission(); // 0 = ACK

template<uint32_t BASE>
class I2C {
public:
    static constexpr uint32_t CLK_HZ = 25000000UL;

    // ── Low-level init ────────────────────────────────────────────────────────
    void Init(uint16_t prescale) const {
        reg(I2C_REG_PRESC_LO) = prescale & 0xFFu;
        reg(I2C_REG_PRESC_HI) = (prescale >> 8) & 0xFFu;
    }

    bool WaitBusy() const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_CMD_EMPTY) == 0 && timeout > 0) { --timeout; }
        if (timeout == 0) return false;

        timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_STATUS) & I2C_STATUS_BUSY) && timeout > 0) { --timeout; }
        return timeout > 0;
    }

    bool NackDetected() const {
        return (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) != 0;
    }

    void ClearNack() const {
        reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
    }

    bool PushData(uint8_t data) const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_WR_FULL) && timeout > 0) { --timeout; }
        if (timeout == 0) return false;
        reg(I2C_REG_DATA) = data;
        return true;
    }

    bool PushCmd(uint8_t cmd) const {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        while ((reg(I2C_REG_FIFO) & I2C_FIFO_CMD_FULL) && timeout > 0) { --timeout; }
        if (timeout == 0) return false;
        reg(I2C_REG_CMD) = cmd;
        return true;
    }

    void SetAddress(uint8_t addr) const {
        reg(I2C_REG_ADDR) = addr;
    }

    bool Probe(uint8_t addr7bit) const {
        uint8_t dummy = 0x00u;
        return Write(addr7bit, &dummy, 1) == 0;
    }

    int Write(uint8_t addr, const uint8_t *buf, uint8_t len) const {
        if (len == 0) return 0;

        if (reg(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) {
            reg(I2C_REG_STATUS) = I2C_STATUS_MISS_ACK;
        }

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
            while ((reg(I2C_REG_FIFO) & I2C_FIFO_RD_EMPTY) && timeout > 0) { --timeout; }
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
    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(BASE + offset);  // compile-time constant
    }

    // ── Wire API state ────────────────────────────────────────────────────────
    // Zero-initialised by startup BSS zeroing (which now covers .sbss too).
    uint8_t tx_address_;
    uint8_t tx_buffer_[I2C_TX_BUFFER_SIZE];
    uint8_t tx_length_;
    uint8_t rx_buffer_[I2C_RX_BUFFER_SIZE];
    uint8_t rx_length_;
    uint8_t rx_position_;

public:
    // ── Arduino Wire-compatible API ───────────────────────────────────────────

    // Initialise bus at 400 kHz (prescale = CLK_HZ / (400000 * 4) = 15 @ 25 MHz).
    void begin() { Init((uint16_t)(CLK_HZ / (400000UL * 4UL))); }

    // Set bus clock; call before begin() if a non-default frequency is needed.
    void setClock(uint32_t freq) {
        Init((uint16_t)(CLK_HZ / (freq * 4UL)));
    }

    void beginTransmission(uint8_t addr) {
        tx_address_ = addr;
        tx_length_  = 0;
    }

    uint8_t write(uint8_t value) {
        if (tx_length_ >= I2C_TX_BUFFER_SIZE) return 0;
        tx_buffer_[tx_length_++] = value;
        return 1;
    }

    // sendStop is accepted for API compatibility; we always issue a stop for now.
    uint8_t endTransmission(bool /*sendStop*/ = true) {
        if (tx_length_ == 0) return 4;
        int rc = Write(tx_address_, tx_buffer_, tx_length_);
        tx_length_ = 0;
        if (rc == 0) return 0;  // success
        if (rc == 1) return 2;  // NACK (address or data)
        return 4;               // timeout / other error
    }

    uint8_t requestFrom(uint8_t addr, uint8_t qty, bool /*stop*/ = true) {
        rx_length_   = 0;
        rx_position_ = 0;
        if (qty == 0 || qty > I2C_RX_BUFFER_SIZE) return 0;
        if (Read(addr, rx_buffer_, qty) != 0) return 0;
        rx_length_ = qty;
        return qty;
    }

    int available() const { return (int)(rx_length_ - rx_position_); }

    int read() {
        if (rx_position_ >= rx_length_) return -1;
        return rx_buffer_[rx_position_++];
    }

    int peek() const {
        if (rx_position_ >= rx_length_) return -1;
        return rx_buffer_[rx_position_];
    }

    void flush() {}
};

#endif /* I2C_H */
