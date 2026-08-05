#include "i2c.h"

void WbI2c::begin() {
    reg_write32(REG_PRESCALE_LO, DEFAULT_PRESCALE & 0xFFu);
    reg_write32(REG_PRESCALE_HI, (DEFAULT_PRESCALE >> 8) & 0xFFu);
    reg_write32(REG_STATUS, 0u);
    reg_write32(REG_CMD, 0u);
    reg_write32(REG_ADDR, 0u);
    reg_write32(REG_DATA, 0u);
    active_tx_ = false;
    tx_started_ = false;
    rx_count_ = 0u;
    rx_index_ = 0u;
}

void WbI2c::setClock(uint32_t clockSpeedHz) {
    if (clockSpeedHz == 0u) {
        clockSpeedHz = 100000u;
    }

    uint32_t prescale = 25000000u / (4u * clockSpeedHz);
    if (prescale == 0u) {
        prescale = 1u;
    }
    if (prescale > 0xFFFFu) {
        prescale = 0xFFFFu;
    }

    reg_write32(REG_PRESCALE_LO, prescale & 0xFFu);
    reg_write32(REG_PRESCALE_HI, (prescale >> 8) & 0xFFu);
}

void WbI2c::beginTransmission(uint8_t slaveAddr) {
    slave_addr_ = slaveAddr;
    active_tx_ = true;
    tx_started_ = false;
    rx_count_ = 0u;
    rx_index_ = 0u;
    reg_write32(REG_ADDR, static_cast<uint32_t>(slaveAddr));
}

uint8_t WbI2c::write(uint8_t b) {
    if (!active_tx_) {
        beginTransmission(slave_addr_);
    }

    if (!tx_started_) {
        reg_write32(REG_DATA, static_cast<uint32_t>((slave_addr_ << 1u) & 0xFEu));
        reg_write32(REG_CMD, CMD_START_WRITE);
        waitForDone();
        tx_started_ = true;
    }

    reg_write32(REG_DATA, static_cast<uint32_t>(b));
    reg_write32(REG_CMD, CMD_WRITE);
    waitForDone();
    return 1u;
}

uint8_t WbI2c::requestFrom(uint8_t slaveAddr, uint8_t numBytes) {
    if (numBytes == 0u) {
        return 0u;
    }

    slave_addr_ = slaveAddr;
    active_tx_ = false;
    tx_started_ = false;
    rx_count_ = numBytes;
    rx_index_ = 0u;

    reg_write32(REG_DATA, static_cast<uint32_t>((slave_addr_ << 1u) & 0xFEu));
    reg_write32(REG_CMD, CMD_START_WRITE);
    waitForDone();

    reg_write32(REG_DATA, static_cast<uint32_t>((slave_addr_ << 1u) | 0x01u));
    reg_write32(REG_CMD, CMD_START_WRITE);
    waitForDone();

    for (uint8_t i = 0u; i < numBytes; ++i) {
        const uint32_t cmd = (i + 1u < numBytes) ? CMD_READ_ACK : CMD_READ_NACK;
        reg_write32(REG_CMD, cmd);
        waitForDone();
        rx_buffer_[i] = static_cast<uint8_t>(reg_read32(REG_DATA));
    }

    reg_write32(REG_CMD, CMD_STOP);
    waitForDone();
    return numBytes;
}

uint8_t WbI2c::read(void) {
    if (rx_index_ >= rx_count_) {
        return 0u;
    }
    return rx_buffer_[rx_index_++];
}

uint8_t WbI2c::endTransmission() {
    if (active_tx_) {
        reg_write32(REG_CMD, CMD_WRITE_STOP);
        waitForDone();
        active_tx_ = false;
        tx_started_ = false;
    }
    return 0u;
}