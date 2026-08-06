#include "i2c.h"

bool I2c::probe(uint8_t addr)
{
    uint8_t dummy = 0u;

    for (int attempt = 0; attempt < 3; ++attempt) {
        clear_nack();
        write(I2C_REG_CMD, I2C_CMD_STOP);
        (void)wait_idle(I2C_TIMEOUT_LOOP);
        if (write(addr, &dummy, 1u) == I2C_OK) {
            return true;
        }
    }

    return false;
}




// Private methods for low-level I2C operations
bool I2c::wait_idle(uint32_t timeout_ms)
{
    const uint32_t start = millis();

    while (1) {
        const uint32_t fifo_stat = read(I2C_REG_FIFO_STATUS);
        const uint32_t status = read(I2C_REG_STATUS);

        const bool cmd_fifo_empty = (fifo_stat & I2C_FIFO_CMD_EMPTY) != 0u;
        const bool bus_busy = (status & I2C_STATUS_BUSY) != 0u;

        if (cmd_fifo_empty && !bus_busy) {
            __asm__ volatile("" ::: "memory");
            return true;
        }

        if ((millis() - start) >= timeout_ms) {
            __asm__ volatile("" ::: "memory");
            return false;
        }

        __asm__ volatile("nop" ::: "memory");
    }
}

bool I2c::wait_for_cmd_fifo_empty(uint32_t timeout_ms)
{
    const uint32_t start = millis();

    while (1) {
        const uint32_t fifo_stat = read(I2C_REG_FIFO_STATUS);

        if ((fifo_stat & I2C_FIFO_CMD_EMPTY) != 0u) {
            __asm__ volatile("" ::: "memory");
            return true;
        }

        if ((millis() - start) >= timeout_ms) {
            __asm__ volatile("" ::: "memory");
            return false;
        }

        __asm__ volatile("nop" ::: "memory");
    }
}

int I2c::write(uint8_t addr, const uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C_OK;
    }

    if (nack_detected()) {
        clear_nack();
    }

    if (!wait_idle(I2C_TIMEOUT_LOOP)) {
        return I2C_TIMEOUT;
    }

    set_address(addr);

    for (size_t i = 0; i < len; ++i) {
        if (!push_data(buf[i])) {
            return I2C_FIFO_ERROR;
        }

        const uint8_t cmd = (i == 0u)
            ? static_cast<uint8_t>(I2C_CMD_START | I2C_CMD_WRITE)
            : static_cast<uint8_t>(I2C_CMD_WRITE);

        if (!push_cmd(cmd)) {
            return I2C_FIFO_ERROR;
        }

        if (!wait_for_cmd_fifo_empty(I2C_TIMEOUT_LOOP)) {
            return I2C_TIMEOUT;
        }
    }

    if (!push_cmd(I2C_CMD_STOP)) {
        return I2C_FIFO_ERROR;
    }

    if (!wait_idle(I2C_TIMEOUT_LOOP)) {
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

    if (!wait_idle(I2C_TIMEOUT_LOOP)) {
        return I2C_TIMEOUT;
    }

    if (nack_detected()) {
        clear_nack();
        return I2C_NACK;
    }

    return I2C_OK;
}    