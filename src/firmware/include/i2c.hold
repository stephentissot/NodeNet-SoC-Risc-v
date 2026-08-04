#ifndef I2C_H
#define I2C_H

#include <cstddef>
#include <cstdint>
#include "bigsister.h"
#include "lib/i2c/i2c.h"

enum i2c_result {
    I2C_OK = 0,
    I2C_NACK = 1,
    I2C_TIMEOUT = 2,
    I2C_FIFO_ERROR = 3
};

class I2C {
public:
    explicit I2C(uint32_t base) : base_(base), impl_(base) {}

    void init(uint16_t prescale)
    {
        (void)prescale;
        impl_.begin();
        impl_.setClock(100000u);
    }

    void set_address(uint8_t addr)
    {
        impl_.beginTransmission(addr);
    }

    int i2c_write(uint8_t addr, const uint8_t *buf, size_t len)
    {
        if (len == 0u) {
            return I2C_OK;
        }

        impl_.beginTransmission(addr);
        for (size_t i = 0; i < len; ++i) {
            if (!impl_.write(buf[i])) {
                return I2C_FIFO_ERROR;
            }
        }

        const uint8_t status = impl_.endTransmission();
        return (status == 0u) ? I2C_OK : I2C_NACK;
    }

    int i2c_read(uint8_t addr, uint8_t *buf, size_t len)
    {
        if (len == 0u || buf == nullptr) {
            return I2C_OK;
        }

        const uint8_t status = impl_.requestFrom(addr, static_cast<uint8_t>(len));
        if (status != 0u) {
            return I2C_NACK;
        }

        for (size_t i = 0; i < len; ++i) {
            buf[i] = impl_.read();
        }
        return I2C_OK;
    }

    bool probe(uint8_t addr)
    {
        uint8_t dummy = 0u;
        return i2c_write(addr, &dummy, 1u) == I2C_OK;
    }

    int probe_status(uint8_t addr)
    {
        uint8_t dummy = 0u;
        return i2c_write(addr, &dummy, 1u);
    }

private:
    uint32_t base_;
    ZipCpuI2C impl_;
};

#endif /* I2C_H */