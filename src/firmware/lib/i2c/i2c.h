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

#define I2C0_BASE  0x10005000UL

#ifndef I2C_TIMEOUT_LOOP
#define I2C_TIMEOUT_LOOP 25000u
#endif

/* Register offsets (4-byte stride from base). */
#define I2C_REG_STATUS      0x00u
#define I2C_REG_FIFO_STATUS 0x04u
#define I2C_REG_ADDR        0x08u
#define I2C_REG_CMD         0x0Cu
#define I2C_REG_DATA        0x10u
#define I2C_RESERVED        0x14u
#define I2C_REG_PRESC_LO    0x18u
#define I2C_REG_PRESC_HI    0x1Cu

/* Status bits. */
#define I2C_STATUS_BUSY      (1u << 0)
#define I2C_STATUS_BUS_CTRL  (1u << 1)
#define I2C_STATUS_BUS_ACT   (1u << 2)
#define I2C_STATUS_MISS_ACK  (1u << 3)

/* FIFO status bits. */
#define I2C_FIFO_CMD_EMPTY   (1u << 0)
#define I2C_FIFO_CMD_FULL    (1u << 1)
#define I2C_FIFO_CMD_OVF     (1u << 2)
#define I2C_FIFO_WR_EMPTY    (1u << 3)
#define I2C_FIFO_WR_FULL     (1u << 4)
#define I2C_FIFO_WR_OVF      (1u << 5)
#define I2C_FIFO_RD_EMPTY    (1u << 6)
#define I2C_FIFO_RD_FULL     (1u << 7)

/* Command bits. */
#define I2C_CMD_START        (1u << 0)
#define I2C_CMD_READ         (1u << 1)
#define I2C_CMD_WRITE        (1u << 2)
#define I2C_CMD_STOP         (1u << 4)

class I2c {    
public:
    static constexpr uint32_t CLK_KHZ = 25000u;  // 25 MHz
    explicit I2c(void) {}

    // Initialize the I2C peripheral with a prescale value for the desired clock speed : 62 for 100 kHz, 15 for 400 kHz at 25 MHz clock
    void begin(void) {
        init(15); // 400 kHz @ 25 MHz
    }
    void begin(uint16_t prescale) {
        init(prescale);
    }

    void beginTransmission(uint8_t address);

    uint8_t probe(uint8_t addr)
    {
        clear_nack();
        set_address(addr);
        push_data(0x00u); // dummy data to trigger address phase
        //*i2c_cmd_reg  = I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP;
        push_cmd(static_cast<uint8_t>(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP));

        if(!wait_idle()) {
            return 2u; // timeout
        }

        if ((static_cast<uint32_t>(*i2c_status) & I2C_STATUS_MISS_ACK) != 0u) {
            *i2c_status = I2C_STATUS_MISS_ACK; // clear again
            return 1u; // nack
        }

        return 0u; // ok
    }

    enum result {
        I2C_OK = 0,
        I2C_NACK = 1,
        I2C_TIMEOUT = 2,
        I2C_FIFO_ERROR = 3
    };

private:
    volatile uint32_t* const i2c_addr_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C_REG_ADDR);
    volatile uint32_t* const i2c_data_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C_REG_DATA);
    volatile uint32_t* const i2c_cmd_reg  = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C_REG_CMD);
    volatile uint32_t* const i2c_fifo_status_reg  = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C_REG_FIFO_STATUS);
    volatile uint32_t* const i2c_status  = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C_REG_STATUS);

    // Set i2c device address for the next transaction
    void set_address(uint8_t addr)
    {
        *i2c_addr_reg = static_cast<uint32_t>(addr);
    }

    // push_command pushes a command byte to the I2C command FIFO, returning true if successful, false if timeout occurs (fifo still full after timeout)
    bool push_cmd(uint8_t cmd){
        if(wait_for_cmd_fifo_empty(I2C_TIMEOUT_LOOP)){
            *i2c_cmd_reg = static_cast<uint32_t>(cmd);
            return true;
        }
        return false; // timeout
    }

    // push_data pushes a data byte to the I2C data FIFO, returning true if successful, false if timeout occurs (fifo still full after timeout)
    bool push_data(uint8_t data){
        if(wait_for_wdata_fifo_empty(I2C_TIMEOUT_LOOP)){
            *i2c_data_reg = static_cast<uint32_t>(data);
            return true;
        }
        return false; // timeout
    }

    // Wait for the I2C peripheral to become idle (busy is false), with a timeout, returning true if idle, false if timeout
    bool wait_idle(uint32_t timeout = I2C_TIMEOUT_LOOP) {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while ((static_cast<uint32_t>(*i2c_status) & I2C_STATUS_BUSY) != 0u && t > 0u) {
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        if (t == 0u) {
            return false; // timeout
        }
        return true; // idle
    }

    // Wait for the fifo to be available for a command, with a timeout, returning true if fifo not full, false if timeout
    bool wait_for_cmd_fifo_empty(uint32_t timeout_ms) {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while ((static_cast<uint32_t>(*i2c_fifo_status_reg) & I2C_FIFO_CMD_FULL) != 0u && t > 0u) {
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        if (t == 0u) {
            return false; // timeout
        }
        return true; // idle
    }

    // Wait for the fifo to be available to write, with a timeout, returning true if fifo not full, false if timeout
    bool wait_for_wdata_fifo_empty(uint32_t timeout_ms) {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while ((static_cast<uint32_t>(*i2c_fifo_status_reg) & I2C_FIFO_WR_FULL) != 0u && t > 0u) {
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        if (t == 0u) {
            return false; // timeout
        }
        return true; // idle
    }
    
    // Clear prior miss-ack flag
    void clear_nack(void)
    {
       *i2c_status = I2C_STATUS_MISS_ACK;
    }
















    uintptr_t i2c_addr(uint32_t reg)
    {
        return static_cast<uintptr_t>(base_ + reg);
    }

    uint32_t read(uint32_t reg)
    {
        volatile uint32_t *addr =
            reinterpret_cast<volatile uint32_t *>(i2c_addr(reg));
        return *addr;
    }

    void write(uint32_t reg, uint32_t value)
    {
        volatile uint32_t *addr =
            reinterpret_cast<volatile uint32_t *>(i2c_addr(reg));
        *addr = value;
    }

    uint32_t base_;

    // private functions and methods for low-level I2C operations can be added here

    int write(uint8_t addr, const uint8_t *buf, size_t len);
    int read(uint8_t addr, uint8_t *buf, size_t len);

    // Inline private methods for low-level I2C operations

    // nack detected means no device responded at the last address to data byte sent
    bool nack_detected(void)
    {
        return (read(I2C_REG_STATUS) & I2C_STATUS_MISS_ACK) != 0u;
    }



    bool notReady(void)
    {
        return (read(I2C_REG_STATUS) & I2C_STATUS_BUSY) != 0u;
    }

    void init(uint16_t prescale)
    {
        write(I2C_REG_PRESC_LO, (uint32_t)(prescale & 0xFFu));
        write(I2C_REG_PRESC_HI, (uint32_t)((prescale >> 8) & 0xFFu));
        clear_nack();
        write(I2C_REG_CMD, I2C_CMD_STOP);
        (void)wait_idle(I2C_TIMEOUT_LOOP);
        set_address(0x00); // Clear address register
    }
   
};

#endif /* I2C_H */

