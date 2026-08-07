#include "i2c.h"


// Private methods for low-level I2C operations

int I2c::write(uint8_t addr, const uint8_t *buf, size_t len)
{
    
    if (len == 0u) return I2C_OK;
    if (nack_detected()) clear_nack();
    set_address(addr);
    for (size_t i = 0; i < len; ++i) {
        if (!push_data(buf[i])) return I2C_FIFO_ERROR;
        uint8_t cmd = 0u;
        if(len == 1u){
            cmd = static_cast<uint8_t>(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP);
            if (!push_cmd(cmd)) return I2C_FIFO_ERROR;
        }
        else{
            if (i == 0u) {
                cmd = static_cast<uint8_t>(I2C_CMD_START | I2C_CMD_WRITE);
            } else if (i + 1u == len) {
                cmd = static_cast<uint8_t>(I2C_CMD_WRITE | I2C_CMD_STOP);
            } else {
                cmd = static_cast<uint8_t>(I2C_CMD_WRITE);
            }
            if (!push_cmd(cmd)) return I2C_FIFO_ERROR;
        } 
    }

    if (!wait_idle()) {
        return I2C_TIMEOUT;
    }

    if (nack_detected()) {
        clear_nack();
        return I2C_NACK;
    }

    return I2C_OK;
}

int I2c::read(uint8_t addr, uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C_OK;
    }

    if (nack_detected()) {
        clear_nack();
    }

    set_address(addr);

    for (size_t i = 0; i < len; ++i) {
        uint8_t cmd = (i == 0u) ? (I2C_CMD_START | I2C_CMD_READ) : I2C_CMD_READ;
        if (i + 1u == len) {
            cmd |= I2C_CMD_STOP;
        }
        if (!push_cmd(cmd)) {
            return I2C_FIFO_ERROR;
        }
    }

    for (size_t i = 0; i < len; ++i) {
        const uint32_t start = millis();
        while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_RD_EMPTY) != 0u) {
            if ((millis() - start) >= I2C_TIMEOUT_LOOP) {
                return I2C_TIMEOUT;
            }
        }
        buf[i] = (uint8_t)(read(I2C_REG_DATA) & 0xFFu);
    }

    if (!wait_idle()) {
        return I2C_TIMEOUT;
    }

    if (nack_detected()) {
        clear_nack();
        return I2C_NACK;
    }

    return I2C_OK;
}    