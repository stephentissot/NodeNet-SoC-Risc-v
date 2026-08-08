#ifndef I2C_H
#define I2C_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include "bigsister.h"
/*
 * Bare-metal Wishbone I2C driver for wb_i2c.sv.
 *
 * The driver is written for the Alex Forencich i2c_master_wbs_16 core
 * exposed behind the wb_i2c wrapper. The peripheral is memory-mapped at
 * 0x10005000 and exposed as 32-bit MMIO registers with a 4-byte word stride.
 *
 * Safety with optimization:
 *   - every MMIO access is done through volatile pointers,
 *   - compiler barriers are inserted around each read/write,
 *   - polling loops never assume the compiler will re-read the register.
 */

#define I2C0_BASE  0x10005000UL


#define I2C_TIMEOUT_LOOP 25000u


/* Register offsets (4-byte stride from base). */
#define I2C16_REG_STATUS 0x00u
#define I2C16_REG_CMD 0x04u
#define I2C16_REG_DATA 0x08u
#define I2C16_REG_PRESC 0x0Cu

/* STATUS (0x00) */
#define I2C16_STATUS_BUSY (1u << 0)
#define I2C16_STATUS_BUS_CTRL (1u << 1)
#define I2C16_STATUS_BUS_ACT (1u << 2)
#define I2C16_STATUS_MISS_ACK (1u << 3)

/* FIFO summary bits are in STATUS[15:8] */
#define I2C16_FIFO_CMD_EMPTY (1u << 8)
#define I2C16_FIFO_CMD_FULL (1u << 9)
#define I2C16_FIFO_CMD_OVF (1u << 10)
#define I2C16_FIFO_WR_EMPTY (1u << 11)
#define I2C16_FIFO_WR_FULL (1u << 12)
#define I2C16_FIFO_WR_OVF (1u << 13)
#define I2C16_FIFO_RD_EMPTY (1u << 14)
#define I2C16_FIFO_RD_FULL (1u << 15)

/* CMD (0x04): addr in [6:0], command bits in [12:8] */
#define I2C16_CMD_ADDR_MASK (0x7Fu)
#define I2C16_CMD_START (1u << 8)
#define I2C16_CMD_READ (1u << 9)
#define I2C16_CMD_WRITE (1u << 10)
#define I2C16_CMD_WRITE_MULT (1u << 11)
#define I2C16_CMD_STOP (1u << 12)

/* DATA (0x08) */
#define I2C16_DATA_BYTE_MASK (0xFFu)
#define I2C16_DATA_VALID (1u << 8)
#define I2C16_DATA_LAST (1u << 9)

/* PRESC (0x0C) */
#define I2C16_PRESC_MASK (0xFFFFu)

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

    uint8_t probe(const uint8_t addr)
    {
        return write(addr, 0x00u); // send a dummy data byte to trigger address phase
    }

    // Write a single byte to the I2C bus at the given address, returning an I2C result code
    uint8_t write(const uint8_t addr, const uint8_t payload)
    {
        clear_nack();        
        if(!push_data(payload)) return I2C_FIFO_ERROR; // dummy data to trigger address phase
        if(!push_cmd(addr, static_cast<uint16_t>(I2C16_CMD_START | I2C16_CMD_WRITE | I2C16_CMD_STOP))) return I2C_CMD_ERROR;

        if(!wait_tx_complete()) {
            return I2C_TIMEOUT; // timeout
        }
        if (nack_detected()) {
            return I2C_NACK; // nack
        }
        return I2C_OK; // ok
    }

    // Write two byte to the I2C bus at the given address, returning an I2C result code
    uint8_t write2(const uint8_t addr, const uint8_t payload1, const uint8_t payload2)
    {
        clear_nack();
        if(!push_data(payload1)) return I2C_FIFO_ERROR; // dummy data to trigger address phase
        __asm__ volatile("" ::: "memory");
        if(!push_data(payload2)) return I2C_FIFO_ERROR; // dummy data to trigger address phase
        __asm__ volatile("" ::: "memory");
        if(!push_cmd(addr, static_cast<uint16_t>(I2C16_CMD_START | I2C16_CMD_WRITE))) return I2C_CMD_ERROR;
        __asm__ volatile("" ::: "memory");
        if(!push_cmd(addr, static_cast<uint16_t>(I2C16_CMD_WRITE | I2C16_CMD_STOP))) return I2C_CMD_ERROR;
        __asm__ volatile("" ::: "memory");
        if(!wait_tx_complete()) {
            return I2C_TIMEOUT; // timeout
        }
        if (nack_detected()) {
            return I2C_NACK; // nack
        }
        return I2C_OK; // ok
    }

    // Write multiple bytes to the I2C bus at the given address, returning an I2C result code
    uint8_t write(const uint8_t addr, const uint8_t* buf, const size_t len)
    {
        if (buf == nullptr || len == 0u) return I2C_CMD_ERROR;
        if(len == 1u) return write(addr, buf[0]); // single byte write
        clear_nack();
        if(!push_cmd(addr, static_cast<uint16_t>(I2C16_CMD_START | I2C16_CMD_WRITE_MULT | I2C16_CMD_STOP))) return I2C_CMD_ERROR; // start + write_multi + stop
        for (size_t i = 0; i < len; ++i) {
            if(!push_data(buf[i], (i == len - 1u))) return I2C_FIFO_ERROR;
        }
        if (!wait_tx_complete()) {
            return I2C_TIMEOUT; // timeout
        }
        if (nack_detected()) {
            return I2C_NACK; // nack
        }
        return I2C_OK; // ok
    }
    // Write multiple bytes to the I2C bus at the given address, returning an I2C result code
    uint8_t write(const uint8_t addr, std::initializer_list<uint8_t> data)
    {
        return write(addr, data.begin(), data.size());
    }

    // Write up to len bytes from an initializer list.
    // This enables calls like: Wire.write(0x3C, {0x00, 0xAF}, 2)
    uint8_t write(const uint8_t addr, std::initializer_list<uint8_t> data, const size_t len)
    {
        if (len > data.size()) return I2C_CMD_ERROR;
        return write(addr, data.begin(), len);
    }

    bool isBusy() const {        
        return ((*i2c_status_reg & I2C16_STATUS_BUSY) != 0u);
    }
    bool isBusActive() {
        return ((*i2c_status_reg & I2C16_STATUS_BUS_ACT) != 0u);
    }
    bool isBusControlled() {
        return ((*i2c_status_reg & I2C16_STATUS_BUS_CTRL) != 0u);
    }

    enum i2cResult {
        I2C_OK = 0,
        I2C_NACK = 1,
        I2C_TIMEOUT = 2,
        I2C_FIFO_ERROR = 3,
        I2C_CMD_ERROR = 4
    };

private:

    volatile uint32_t* const i2c_status_reg  = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C16_REG_STATUS);
    volatile uint32_t* const i2c_cmd_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C16_REG_CMD);
    volatile uint32_t* const i2c_data_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C16_REG_DATA);
    volatile uint32_t* const i2c_prescaler  = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + I2C16_REG_PRESC);
    

    void init(uint16_t prescale)
    {
        *i2c_prescaler = static_cast<uint32_t>(prescale);
        __asm__ volatile("" ::: "memory");
        clear_nack();   
    }

    bool push_cmd(uint8_t addr, uint16_t cmd){
        if(!wait_for_cmd_fifo_space()) return false; // timeout

        const uint16_t a = static_cast<uint16_t>(addr) & I2C16_CMD_ADDR_MASK;
        const uint16_t c = cmd & (I2C16_CMD_START | I2C16_CMD_READ | I2C16_CMD_WRITE | I2C16_CMD_WRITE_MULT | I2C16_CMD_STOP);
        const uint16_t word = a | c;
        *i2c_cmd_reg = static_cast<uint32_t>(word);
        __asm__ volatile("" ::: "memory");
        return true;
    }

    // push_data pushes a data byte to the I2C data FIFO, returning true if successful, false if timeout occurs (fifo still full after timeout)
    bool push_data(uint8_t data, bool last = false, bool valid = true){
        if(!wait_for_data_fifo_space()) return false; // timeout
        uint16_t word = static_cast<uint16_t>(data & I2C16_DATA_BYTE_MASK);
        if(last) {
            word |= I2C16_DATA_LAST;
        }
        if(valid) {
            word |= I2C16_DATA_VALID;
        }
        *i2c_data_reg = static_cast<uint32_t>(word);
        __asm__ volatile("" ::: "memory");
        return true;
    }

    // Wait for the I2C peripheral to complete the current transaction, with a timeout, returning true if idle, false if timeout
    bool wait_tx_complete() {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while (t > 0u) {
            const uint32_t s = *i2c_status_reg;
            const bool busy = (s & I2C16_STATUS_BUSY) != 0u;
            const bool bus_ctrl = (s & I2C16_STATUS_BUS_CTRL) != 0u;
            const bool cmd_empty = (s & I2C16_FIFO_CMD_EMPTY) != 0u;
            const bool wr_empty = (s & I2C16_FIFO_WR_EMPTY) != 0u;

            if (!busy && !bus_ctrl && cmd_empty && wr_empty) return true;
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        return false;
    }

    // Wait for the I2C peripheral to become idle (busy is false), with a timeout, returning true if idle, false if timeout
    bool wait_idle() {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while (t > 0u) {
            if(!isBusy()) break; // extra check to avoid infinite loop if status reg is stuck
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        if (t == 0u) {
            return false; // timeout
        }

        t = I2C_TIMEOUT_LOOP;
        while (t > 0u) {
            if(!isBusControlled()) break; // wait for bus to be released
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        return true; // idle
    }

    // Wait for the fifo to be available for a command, with a timeout, returning true if fifo not full, false if timeout
    bool wait_for_cmd_fifo_space() {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while ((static_cast<uint32_t>(*i2c_status_reg) & I2C16_FIFO_CMD_FULL) != 0u && t > 0u) {
            --t;
            __asm__ volatile("nop" ::: "memory");
        }
        if (t == 0u) {
            return false; // timeout
        }
        return true; // idle
    }

    // Wait for the fifo to be available to write data, with a timeout, returning true if fifo not full, false if timeout
    bool wait_for_data_fifo_space() {
        uint32_t t = I2C_TIMEOUT_LOOP;
        while ((static_cast<uint32_t>(*i2c_status_reg) & I2C16_FIFO_WR_FULL) != 0u && t > 0u) {
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
       *i2c_status_reg = I2C16_STATUS_MISS_ACK;
       __asm__ volatile("" ::: "memory");
    }

    // nack detected means no device responded at the last address to data byte sent
    bool nack_detected(void)
    {
        if ((static_cast<uint32_t>(*i2c_status_reg) & I2C16_STATUS_MISS_ACK) != 0u) {
            clear_nack(); // clear again
            return true; // nack
        }
        return false; // ok
    }
    void waitNop(uint32_t timeout);
   
};

#endif /* I2C_H */

