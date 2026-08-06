#include "i2c.h"

bool I2c::probe(uint8_t addr)
{
    uint8_t dummy = 0u;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (write(addr, &dummy, 1u) == I2C_OK) {
            return true;
        }
    }

    return false;
}


// Private methods for low-level I2C operations
bool I2c::wait_idle(uint32_t timeout)
{
    uint32_t t = timeout != 0u ? timeout * 15000u  : I2C_TIMEOUT_LOOP * 15000u; // Convert milliseconds to loop iterations (approximate)
    while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_CMD_EMPTY) == 0u && t > 0u) {
        --t;
    }
    if (t == 0u) {
        return false;
    }

    t = timeout != 0u ? timeout : I2C_TIMEOUT_LOOP;
    while ((read(I2C_REG_STATUS) & I2C_STATUS_BUSY) != 0u && t > 0u) {
        --t;
    }
    return t > 0u;
}

// bool I2c::wait_idle(uint32_t timeout_ms)
// {
//     const uint32_t start = millis(); 

//     while (1) {
//         const uint32_t fifo_stat = read(I2C_REG_FIFO_STATUS);
//         const uint32_t status = read(I2C_REG_STATUS);

//         const bool cmd_fifo_empty = (fifo_stat & I2C_FIFO_CMD_EMPTY) != 0u;
//         const bool bus_busy = (status & I2C_STATUS_BUSY) != 0u;

//         if (cmd_fifo_empty && !bus_busy) {
//             __asm__ volatile("" ::: "memory");
//             return true;
//         }

//         if ((millis() - start) >= timeout_ms) {
//             __asm__ volatile("" ::: "memory");
//             return false;
//         }

//         __asm__ volatile("nop" ::: "memory");
//     }
// }

// bool I2c::wait_for_cmd_fifo_empty(uint32_t timeout_ms)
// {
//     const uint32_t start = millis();

//     while (1) {
//         const uint32_t fifo_stat = read(I2C_REG_FIFO_STATUS);

//         if ((fifo_stat & I2C_FIFO_CMD_EMPTY) != 0u) {
//             __asm__ volatile("" ::: "memory");
//             return true;
//         }

//         if ((millis() - start) >= timeout_ms) {
//             __asm__ volatile("" ::: "memory");
//             return false;
//         }

//         __asm__ volatile("nop" ::: "memory");
//     }
// }
// static inline int i2c_write(uint8_t addr,
//                              const uint8_t *buf, uint8_t len)
// {
//     i2c_trace_toggle();
//     if (len == 0) return 0;
//     if (i2c_nack_detected()) i2c_clear_nack();
//     i2c_set_address(addr);
//     if (len == 1) {
//         if (!i2c_push_data(buf[0])) return 2;
//         if (!i2c_push_cmd(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
//     } else {
//         if (!i2c_push_data(buf[0])) return 2;
//         if (!i2c_push_cmd(I2C_CMD_START | I2C_CMD_WRITE)) return 2;
//         for (uint8_t i = 1; i < len - 1; i++) {
//             if (!i2c_push_data(buf[i])) return 2;
//             if (!i2c_push_cmd(I2C_CMD_WRITE)) return 2;
//         }
//         if (!i2c_push_data(buf[len - 1])) return 2;
//         if (!i2c_push_cmd(I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
//     }
//     if (!i2c_wait_busy()) return 2;
//     if (i2c_nack_detected()) { i2c_clear_nack(); return 1; }
//     return 0;
// }
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
        }
        else if (i == 0u) {
            cmd = static_cast<uint8_t>(I2C_CMD_START | I2C_CMD_WRITE);
        } else if (i + 1u == len) {
            cmd = static_cast<uint8_t>(I2C_CMD_WRITE | I2C_CMD_STOP);
        } else {
            cmd = static_cast<uint8_t>(I2C_CMD_WRITE);
        }
        if (!push_cmd(cmd)) return I2C_FIFO_ERROR;
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