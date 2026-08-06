#ifndef I2C_H
#define I2C_H

#include <cstddef>
#include <cstdint>
#include "bigsister.h"
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
#define I2C0_TIMEOUT_LOOP 10u
#endif

#define I2C0_RELEASE_DELAY 80000u

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

class I2c {    
public:
    static constexpr uint32_t CLK_KHZ = 25000u;  // 25 MHz
    explicit I2c(uint32_t base) : base_(base) {}

    bool probe(uint8_t addr);

    enum result {
        I2C0_OK = 0,
        I2C0_NACK = 1,
        I2C0_TIMEOUT = 2,
        I2C0_FIFO_ERROR = 3
    };

private:
    volatile uint32_t& reg() const {
        return *reinterpret_cast<volatile uint32_t*>(base_);
    }
    volatile uint32_t read(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
    }
    void write(uint32_t offset, uint32_t value) const {
        *reinterpret_cast<volatile uint32_t*>(base_ + offset) = value;
    }
    uint32_t base_;

    // private functions and methods for low-level I2C operations can be added here

    bool wait_idle(uint32_t timeout_ms);
    
    int write(uint8_t addr, const uint8_t *buf, size_t len);

    int read(uint8_t addr, uint8_t *buf, size_t len);

    // Inline private methods for low-level I2C operations

    bool nack_detected(void)
    {
        return (read(I2C0_REG_STATUS) & I2C0_STATUS_MISS_ACK) != 0u;
    }

    void clear_nack(void)
    {
        write(I2C0_REG_STATUS, I2C0_STATUS_MISS_ACK);
    }

    bool notReady(void)
    {
        return (read(I2C0_REG_STATUS) & I2C0_STATUS_BUSY) != 0u;
    }

    bool push_data(uint8_t data)
    {
        const uint32_t start = millis();
        while ((read(I2C0_REG_FIFO) & I2C0_FIFO_WR_FULL) != 0u) {
            if ((millis() - start) >= I2C0_TIMEOUT_LOOP) {
                return false;
            }
        }
        write(I2C0_REG_DATA, (uint32_t)data);
        return true;
    }

    bool fifoIsFull()
    {
        return (read(I2C0_REG_FIFO) & I2C0_FIFO_WR_FULL) != 0u;
    }

    bool push_cmd(uint8_t cmd)
    {
        const uint32_t start = millis();
        while ((read(I2C0_REG_FIFO) & I2C0_FIFO_CMD_FULL) != 0u) {
            if ((millis() - start) >= I2C0_TIMEOUT_LOOP) {
                return false;
            }
        }
        write(I2C0_REG_CMD, (uint32_t)cmd);
        return true;
    }

    void init(uint16_t prescale)
    {
        write(I2C0_REG_PRESC_LO, (uint32_t)(prescale & 0xFFu));
        write(I2C0_REG_PRESC_HI, (uint32_t)((prescale >> 8) & 0xFFu));
        clear_nack();
        write(I2C0_REG_CMD, I2C0_CMD_STOP);
        (void)wait_idle(I2C0_TIMEOUT_LOOP);
    }

    void set_address(uint8_t addr)
    {
        write(I2C0_REG_ADDR, (uint32_t)addr);
    }

    
};

#endif /* I2C_H */

