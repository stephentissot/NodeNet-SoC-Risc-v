/**
 * i2c.cpp — Arduino Wire-compatible API for the I2C class.
 *
 * The low-level MMIO methods (Init, Write, Read, PushData, PushCmd, …) are
 * all inline in i2c.h.  Defining endTransmission here means it calls the
 * inline Write() directly — GCC sees the full MMIO logic and cannot produce
 * the incorrect out-of-line indirection that caused crashes under -Os.
 */

#include "i2c.h"

// 400 kHz at 25 MHz: prescale = 25000000 / (400000 * 4) = 15
void I2C::begin() {
    Init((uint16_t)(CLK_HZ / (400000UL * 4UL)));
}

void I2C::setClock(uint32_t freq) {
    Init((uint16_t)(CLK_HZ / (freq * 4UL)));
}

void I2C::beginTransmission(uint8_t addr) {
    tx_address_ = addr;
    tx_length_  = 0;
}

uint8_t I2C::write(uint8_t value) {
    if (tx_length_ >= I2C_TX_BUFFER_SIZE) return 0;
    tx_buffer_[tx_length_++] = value;
    return 1;
}

// Returns: 0=success, 2=NACK, 4=timeout/other  (Arduino Wire convention)
uint8_t I2C::endTransmission(bool /*sendStop*/) {
    if (tx_length_ == 0) return 4;
    int rc = Write(tx_address_, tx_buffer_, tx_length_);
    tx_length_ = 0;
    if (rc == 0) return 0;
    if (rc == 1) return 2;   // NACK on address or data
    return 4;                // timeout / FIFO error
}

uint8_t I2C::requestFrom(uint8_t addr, uint8_t qty, bool /*stop*/) {
    rx_length_   = 0;
    rx_position_ = 0;
    if (qty == 0 || qty > I2C_RX_BUFFER_SIZE) return 0;
    if (Read(addr, rx_buffer_, qty) != 0) return 0;
    rx_length_ = qty;
    return qty;
}

int I2C::available() const {
    return (int)(rx_length_ - rx_position_);
}

int I2C::read() {
    if (rx_position_ >= rx_length_) return -1;
    return rx_buffer_[rx_position_++];
}

int I2C::peek() const {
    if (rx_position_ >= rx_length_) return -1;
    return rx_buffer_[rx_position_];
}

void I2C::flush() {}
