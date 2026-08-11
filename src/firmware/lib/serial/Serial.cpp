#include "Serial.h"

Serial::Serial(uint32_t base_addr, uint32_t clock_hz)
    : data_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_DATA)),
      status_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_STATUS)),
      baud_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_BAUD)),
      clock_hz_(clock_hz),
      has_peeked_(false),
      peeked_byte_(0u) {
}

void Serial::begin(uint32_t baudrate) {
    begin(baudrate, clock_hz_);
}

void Serial::begin(uint32_t baudrate, uint32_t clock_hz) {
    clock_hz_ = clock_hz;
    *baud_reg_ = static_cast<uint32_t>(computePrescale(baudrate, clock_hz_));
    has_peeked_ = false;
    clearErrors();
}

int Serial::available() const {
    if (has_peeked_) {
        return 1;
    }

    const uint32_t s = status();
    if ((s & STATUS_RX_EMPTY) != 0u) {
        return 0;
    }

    if ((s & STATUS_RX_FULL) != 0u) {
        return static_cast<int>(kAssumedFifoDepth);
    }

    return 1;
}

int Serial::availableForWrite() const {
    return ((status() & STATUS_TX_FULL) == 0u) ? 1 : 0;
}

int Serial::peek() {
    if (has_peeked_) {
        return static_cast<int>(peeked_byte_);
    }

    if ((status() & STATUS_RX_EMPTY) != 0u) {
        return -1;
    }

    peeked_byte_ = static_cast<uint8_t>(*data_reg_ & 0xFFu);
    has_peeked_ = true;
    return static_cast<int>(peeked_byte_);
}

int Serial::read() {
    if (has_peeked_) {
        has_peeked_ = false;
        return static_cast<int>(peeked_byte_);
    }

    if ((status() & STATUS_RX_EMPTY) != 0u) {
        return -1;
    }

    return static_cast<int>(*data_reg_ & 0xFFu);
}

std::size_t Serial::write(uint8_t c) {
    if (!waitTxSpace()) {
        return 0;
    }

    *data_reg_ = static_cast<uint32_t>(c);
    return 1;
}

std::size_t Serial::write(const uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0u) {
        return 0;
    }

    std::size_t sent = 0;
    for (; sent < len; ++sent) {
        if (write(data[sent]) == 0u) {
            break;
        }
    }

    return sent;
}

std::size_t Serial::write(const char* text) {
    if (text == nullptr) {
        return 0;
    }

    std::size_t len = 0;
    while (text[len] != '\0') {
        ++len;
    }

    return write(reinterpret_cast<const uint8_t*>(text), len);
}

std::size_t Serial::print(const char* text) {
    return write(text);
}

std::size_t Serial::println(const char* text) {
    std::size_t n = print(text);
    n += println();
    return n;
}

std::size_t Serial::println() {
    static const uint8_t eol[] = {'\r', '\n'};
    return write(eol, sizeof(eol));
}

void Serial::flush() const {
    (void)waitTxIdle();
}

bool Serial::hasOverrunError() const {
    return (status() & STATUS_RX_OVERRUN) != 0u;
}

bool Serial::hasFrameError() const {
    return (status() & STATUS_RX_FRAME) != 0u;
}

void Serial::clearErrors() {
    *status_reg_ = (CLEAR_RX_OVERRUN | CLEAR_RX_FRAME);
}

uint16_t Serial::computePrescale(uint32_t baudrate, uint32_t clock_hz) const {
    if (baudrate == 0u || clock_hz == 0u) {
        return 1u;
    }

    uint32_t prescale = (clock_hz + (baudrate * 4u)) / (baudrate * 8u);

    if (prescale == 0u) {
        prescale = 1u;
    }

    if (prescale > 0xFFFFu) {
        prescale = 0xFFFFu;
    }

    return static_cast<uint16_t>(prescale);
}

bool Serial::waitTxSpace() const {
    uint32_t t = kWriteTimeoutLoops;
    while (t-- > 0u) {
        if ((status() & STATUS_TX_FULL) == 0u) {
            return true;
        }
        __asm__ volatile("nop" ::: "memory");
    }
    return false;
}

bool Serial::waitTxIdle() const {
    uint32_t t = kFlushTimeoutLoops;
    while (t-- > 0u) {
        const uint32_t s = status();
        if ((s & STATUS_TX_EMPTY) != 0u && (s & STATUS_TX_FULL) == 0u) {
            return true;
        }
        __asm__ volatile("nop" ::: "memory");
    }
    return false;
}

uint32_t Serial::status() const {
    return *status_reg_;
}
