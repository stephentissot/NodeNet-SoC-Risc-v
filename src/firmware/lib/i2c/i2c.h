#ifndef I2C_H
#define I2C_H

#include <cstdint>

enum class WbI2cStatus : uint8_t
{
    Init,
    Idle,
    Ready,
    Writing,
    Reading,
    StartTimeout,
    WriteTimeout,
    ReadTimeout,
    StopTimeout,
};

class WbI2c {
public:
    static constexpr uint32_t CLK_KHZ = 25000u;  // 25 MHz
    static constexpr uint32_t REG_STATUS = 0x00u;
    static constexpr uint32_t REG_FIFO = 0x04u;
    static constexpr uint32_t REG_ADDR = 0x08u;
    static constexpr uint32_t REG_CMD = 0x0Cu;
    static constexpr uint32_t REG_DATA = 0x10u;
    static constexpr uint32_t REG_PRESCALE_LO = 0x18u;
    static constexpr uint32_t REG_PRESCALE_HI = 0x1Cu;

    static constexpr uint32_t CMD_START_WRITE = 0x90u;
    static constexpr uint32_t CMD_WRITE = 0x10u;
    static constexpr uint32_t CMD_READ_ACK = 0x20u;
    static constexpr uint32_t CMD_READ_NACK = 0x28u;
    static constexpr uint32_t CMD_WRITE_STOP = 0x50u;
    static constexpr uint32_t CMD_STOP = 0x40u;

    static constexpr uint32_t STATUS_BUSY = 0x01u;
    static constexpr uint32_t STATUS_TIP = 0x02u;
    static constexpr uint32_t STATUS_MISS_ACK = 0x08u;
    static constexpr uint32_t DEFAULT_PRESCALE = 62u;

    explicit WbI2c(uint32_t base) : base_(base) {}

    void begin();
    void setClock(uint32_t clockSpeedHz);
    void beginTransmission(uint8_t slaveAddr);
    uint8_t endTransmission();
    uint8_t requestFrom(uint8_t slaveAddr, uint8_t numBytes);
    uint8_t read(void);
    uint8_t write(uint8_t b);
    WbI2cStatus status() const { return status_; }

private:
    volatile uint32_t reg_read32(uint32_t reg) const {
        return *reinterpret_cast<volatile uint32_t*>(base_ + reg);
    }

    void reg_write32(uint32_t reg, uint32_t value) const {
        volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(base_ + reg);
        *addr = value;
        __asm__ volatile("" ::: "memory");
    }

    bool waitForDone() const {
        for (uint32_t i = 0u; i < 1000000u; ++i) {
            __asm__ volatile("" ::: "memory");
            if ((reg_read32(REG_STATUS) & STATUS_TIP) == 0u) {
                return true;
            }
        }
        return false;
    }

    uint32_t base_;
    uint8_t slave_addr_ = 0u;
    bool active_tx_ = false;
    bool tx_started_ = false;
    uint8_t rx_buffer_[16] = {0u};
    uint8_t rx_count_ = 0u;
    uint8_t rx_index_ = 0u;
    WbI2cStatus status_ = WbI2cStatus::Init;
};

#endif /* I2C_H */

