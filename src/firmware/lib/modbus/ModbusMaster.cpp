#include "ModbusMaster.h"

#include "bigsister.h"

ModbusMaster::ModbusMaster(uint32_t base_addr, uint32_t clock_hz)
    : control_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_CONTROL)),
      status_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_STATUS)),
      uart_div_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_UART_DIV)),
      slave_func_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_SLAVE_FUNC)),
      timeout_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_TIMEOUT)),
      interframe_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_INTERFRAME)),
      retry_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_RETRY)),
      tx_len_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_TX_LEN)),
      tx_data_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_TX_DATA)),
      rx_len_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_RX_LEN)),
      rx_data_reg_(reinterpret_cast<volatile uint32_t*>(base_addr + REG_RX_DATA)),
      clock_hz_(clock_hz),
      timeout_ms_(kDefaultTimeoutMs),
      retries_(kDefaultRetries),
      last_error_(Error::None),
      last_exception_code_(0u),
      last_hw_status_(0u) {
}

void ModbusMaster::begin(uint32_t baudrate, uint32_t timeout_ms, uint8_t retries) {
    timeout_ms_ = timeout_ms;
    retries_ = retries;

    writeReg(uart_div_reg_, computeDivisor(baudrate));
    writeReg(timeout_reg_, computeTimeoutCycles(timeout_ms_));
    writeReg(interframe_reg_, computeInterframeCycles(baudrate));
    writeReg(retry_reg_, retries_);
    writeReg(control_reg_, CTRL_CLEAR_STATUS);

    last_error_ = Error::None;
    last_exception_code_ = 0u;
    last_hw_status_ = 0u;
}

void ModbusMaster::setTimeoutMs(uint32_t timeout_ms) {
    timeout_ms_ = timeout_ms;
    writeReg(timeout_reg_, computeTimeoutCycles(timeout_ms_));
}

void ModbusMaster::setRetries(uint8_t retries) {
    retries_ = retries;
    writeReg(retry_reg_, retries_);
}

void ModbusMaster::setInterframeCharsQ1(uint8_t chars_q1) {
    const uint32_t div = readReg(uart_div_reg_) & 0x000FFFFFu;
    uint32_t baudrate = 1u;
    if (div > 0u) {
        baudrate = (clock_hz_ + (div / 2u)) / div;
        if (baudrate == 0u) {
            baudrate = 1u;
        }
    }
    writeReg(interframe_reg_, computeInterframeCyclesCharsQ1(baudrate, chars_q1));
}

bool ModbusMaster::readCoils(uint8_t slave, uint16_t start_addr, uint16_t quantity, bool* out_values) {
    return readBitFunction(slave, 0x01u, start_addr, quantity, out_values);
}

bool ModbusMaster::readDiscreteInputs(uint8_t slave,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      bool* out_values) {
    return readBitFunction(slave, 0x02u, start_addr, quantity, out_values);
}

bool ModbusMaster::readHoldingRegisters(uint8_t slave,
                                        uint16_t start_addr,
                                        uint16_t quantity,
                                        uint16_t* out_values) {
    return readRegsFunction(slave, 0x03u, start_addr, quantity, out_values);
}

bool ModbusMaster::readInputRegisters(uint8_t slave,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      uint16_t* out_values) {
    return readRegsFunction(slave, 0x04u, start_addr, quantity, out_values);
}

bool ModbusMaster::writeSingleCoil(uint8_t slave, uint16_t coil_addr, bool value) {
    const uint16_t encoded = value ? 0xFF00u : 0x0000u;
    return writeSingleCoilRaw(slave, coil_addr, encoded);
}

bool ModbusMaster::writeSingleCoilRaw(uint8_t slave, uint16_t coil_addr, uint16_t raw_value) {
    uint8_t req[4];
    req[0] = static_cast<uint8_t>((coil_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(coil_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((raw_value >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(raw_value & 0xFFu);

    uint8_t resp[kMaxFrameBytes] = {};
    uint16_t resp_len = 0u;
    if (!runTransaction(slave, 0x05u, req, sizeof(req), resp, &resp_len)) {
        return false;
    }

    if (resp_len != 8u) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if (resp[2] != req[0] || resp[3] != req[1] || resp[4] != req[2] || resp[5] != req[3]) {
        last_error_ = Error::ResponseMismatch;
        return false;
    }

    last_error_ = Error::None;
    return true;
}

bool ModbusMaster::writeMultipleCoils(uint8_t slave,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      const bool* values) {
    if (quantity == 0u || quantity > 1968u || values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const uint16_t byte_count = static_cast<uint16_t>((quantity + 7u) / 8u);
    const uint16_t req_len = static_cast<uint16_t>(5u + byte_count);
    if (req_len > kMaxDataBytes) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[kMaxDataBytes] = {};
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);
    req[4] = static_cast<uint8_t>(byte_count);

    for (uint16_t i = 0; i < quantity; ++i) {
        const uint16_t byte_idx = static_cast<uint16_t>(5u + (i / 8u));
        const uint8_t bit_mask = static_cast<uint8_t>(1u << (i & 0x7u));
        if (values[i]) {
            req[byte_idx] = static_cast<uint8_t>(req[byte_idx] | bit_mask);
        }
    }

    uint8_t resp[kMaxFrameBytes] = {};
    uint16_t resp_len = 0u;
    if (!runTransaction(slave, 0x0Fu, req, static_cast<uint8_t>(req_len), resp, &resp_len)) {
        return false;
    }

    if (resp_len != 8u) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if (resp[2] != req[0] || resp[3] != req[1] || resp[4] != req[2] || resp[5] != req[3]) {
        last_error_ = Error::ResponseMismatch;
        return false;
    }

    last_error_ = Error::None;
    return true;
}

bool ModbusMaster::writeSingleRegister(uint8_t slave, uint16_t reg_addr, uint16_t value) {
    uint8_t req[4];
    req[0] = static_cast<uint8_t>((reg_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(reg_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(value & 0xFFu);

    uint8_t resp[kMaxFrameBytes] = {};
    uint16_t resp_len = 0u;
    if (!runTransaction(slave, 0x06u, req, sizeof(req), resp, &resp_len)) {
        return false;
    }

    if (resp_len != 8u) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if (resp[2] != req[0] || resp[3] != req[1] || resp[4] != req[2] || resp[5] != req[3]) {
        last_error_ = Error::ResponseMismatch;
        return false;
    }

    last_error_ = Error::None;
    return true;
}

bool ModbusMaster::writeMultipleRegisters(uint8_t slave,
                                          uint16_t start_addr,
                                          uint16_t quantity,
                                          const uint16_t* values) {
    if (quantity == 0u || quantity > 123u || values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const uint16_t byte_count = static_cast<uint16_t>(quantity * 2u);
    const uint16_t req_len = static_cast<uint16_t>(5u + byte_count);
    if (req_len > kMaxDataBytes) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[kMaxDataBytes] = {};
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);
    req[4] = static_cast<uint8_t>(byte_count);

    for (uint16_t i = 0; i < quantity; ++i) {
        const uint16_t offset = static_cast<uint16_t>(5u + i * 2u);
        req[offset] = static_cast<uint8_t>((values[i] >> 8) & 0xFFu);
        req[offset + 1u] = static_cast<uint8_t>(values[i] & 0xFFu);
    }

    uint8_t resp[kMaxFrameBytes] = {};
    uint16_t resp_len = 0u;
    if (!runTransaction(slave, 0x10u, req, static_cast<uint8_t>(req_len), resp, &resp_len)) {
        return false;
    }

    if (resp_len != 8u) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if (resp[2] != req[0] || resp[3] != req[1] || resp[4] != req[2] || resp[5] != req[3]) {
        last_error_ = Error::ResponseMismatch;
        return false;
    }

    last_error_ = Error::None;
    return true;
}

ModbusMaster::Error ModbusMaster::lastError() const {
    return last_error_;
}

uint8_t ModbusMaster::lastExceptionCode() const {
    return last_exception_code_;
}

uint32_t ModbusMaster::lastHwStatus() const {
    return last_hw_status_;
}

uint16_t ModbusMaster::regsToU16(uint16_t reg) {
    return reg;
}

int16_t ModbusMaster::regsToI16(uint16_t reg) {
    return static_cast<int16_t>(reg);
}

uint32_t ModbusMaster::regsToU32(uint16_t high_reg, uint16_t low_reg) {
    return (static_cast<uint32_t>(high_reg) << 16) | static_cast<uint32_t>(low_reg);
}

uint32_t ModbusMaster::regsToU32ABCD(uint16_t high_reg, uint16_t low_reg) {
    return regsToU32(high_reg, low_reg);
}

uint32_t ModbusMaster::regsToU32BADC(uint16_t high_reg, uint16_t low_reg) {
    const uint16_t high_swap = static_cast<uint16_t>((high_reg >> 8) | (high_reg << 8));
    const uint16_t low_swap = static_cast<uint16_t>((low_reg >> 8) | (low_reg << 8));
    return regsToU32(high_swap, low_swap);
}

uint32_t ModbusMaster::regsToU32CDAB(uint16_t high_reg, uint16_t low_reg) {
    return regsToU32(low_reg, high_reg);
}

uint32_t ModbusMaster::regsToU32DCBA(uint16_t high_reg, uint16_t low_reg) {
    const uint16_t high_swap = static_cast<uint16_t>((high_reg >> 8) | (high_reg << 8));
    const uint16_t low_swap = static_cast<uint16_t>((low_reg >> 8) | (low_reg << 8));
    return regsToU32(low_swap, high_swap);
}

int32_t ModbusMaster::regsToI32(uint16_t high_reg, uint16_t low_reg) {
    const uint32_t u = regsToU32(high_reg, low_reg);
    return static_cast<int32_t>(u);
}

float ModbusMaster::regsToFloatABCD(uint16_t high_reg, uint16_t low_reg) {
    union {
        uint32_t u;
        float f;
    } conv;
    conv.u = regsToU32ABCD(high_reg, low_reg);
    return conv.f;
}

float ModbusMaster::regsToFloatBADC(uint16_t high_reg, uint16_t low_reg) {
    union {
        uint32_t u;
        float f;
    } conv;
    conv.u = regsToU32BADC(high_reg, low_reg);
    return conv.f;
}

float ModbusMaster::regsToFloatCDAB(uint16_t high_reg, uint16_t low_reg) {
    union {
        uint32_t u;
        float f;
    } conv;
    conv.u = regsToU32CDAB(high_reg, low_reg);
    return conv.f;
}

float ModbusMaster::regsToFloatDCBA(uint16_t high_reg, uint16_t low_reg) {
    union {
        uint32_t u;
        float f;
    } conv;
    conv.u = regsToU32DCBA(high_reg, low_reg);
    return conv.f;
}

uint32_t ModbusMaster::readReg(volatile uint32_t* reg) const {
    return *reg;
}

void ModbusMaster::writeReg(volatile uint32_t* reg, uint32_t value) const {
    *reg = value;
}

uint32_t ModbusMaster::computeDivisor(uint32_t baudrate) const {
    if (baudrate == 0u) {
        return 1u;
    }

    uint32_t divisor = (clock_hz_ + (baudrate / 2u)) / baudrate;
    if (divisor == 0u) {
        divisor = 1u;
    }
    if (divisor > 0xFFFFFu) {
        divisor = 0xFFFFFu;
    }
    return divisor;
}

uint32_t ModbusMaster::computeTimeoutCycles(uint32_t timeout_ms) const {
    if (timeout_ms == 0u) {
        return 1u;
    }

    uint64_t cycles = (static_cast<uint64_t>(clock_hz_) * static_cast<uint64_t>(timeout_ms)) / 1000u;
    if (cycles == 0u) {
        cycles = 1u;
    }
    if (cycles > 0xFFFFFFFFull) {
        cycles = 0xFFFFFFFFull;
    }

    return static_cast<uint32_t>(cycles);
}

uint32_t ModbusMaster::computeInterframeCycles(uint32_t baudrate) const {
    return computeInterframeCyclesCharsQ1(baudrate, 14u);
}

uint32_t ModbusMaster::computeInterframeCyclesCharsQ1(uint32_t baudrate, uint8_t chars_q1) const {
    if (baudrate == 0u) {
        return 1u;
    }

    uint8_t chars_q1_local = chars_q1;
    if (chars_q1_local == 0u) {
        chars_q1_local = 1u;
    }

    // 11 bits/char timing baseline. chars_q1 is quarter-char units.
    // cycles = ceil(clock_hz * (chars_q1/4) * 11 / baud).
    uint64_t numerator = static_cast<uint64_t>(clock_hz_) * static_cast<uint64_t>(chars_q1_local) * 11ull;
    uint64_t denom = static_cast<uint64_t>(baudrate) * 4ull;
    uint64_t cycles = (numerator + denom - 1ull) / denom;

    if (cycles == 0u) {
        cycles = 1u;
    }
    if (cycles > 0xFFFFFFFFull) {
        cycles = 0xFFFFFFFFull;
    }

    return static_cast<uint32_t>(cycles);
}

bool ModbusMaster::runTransaction(uint8_t slave,
                                  uint8_t function,
                                  const uint8_t* req_data,
                                  uint8_t req_len,
                                  uint8_t* resp_frame,
                                  uint16_t* resp_len) {
    if (req_data == nullptr || resp_frame == nullptr || resp_len == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    if (req_len > kMaxDataBytes) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const uint32_t st = readReg(status_reg_);
    if ((st & STATUS_BUSY) != 0u) {
        last_hw_status_ = st;
        last_error_ = Error::Busy;
        return false;
    }

    last_exception_code_ = 0u;
    writeReg(control_reg_, CTRL_CLEAR_STATUS);
    writeReg(retry_reg_, retries_);
    writeReg(slave_func_reg_, (static_cast<uint32_t>(slave) << 8) | static_cast<uint32_t>(function));
    writeReg(tx_len_reg_, static_cast<uint32_t>(req_len));

    for (uint8_t i = 0; i < req_len; ++i) {
        writeReg(tx_data_reg_, static_cast<uint32_t>(req_data[i]));
    }

    writeReg(control_reg_, CTRL_START);

    // Guard timeout is larger than hardware timeout to account for retries.
    const uint32_t guard_timeout_ms = static_cast<uint32_t>((timeout_ms_ * (static_cast<uint32_t>(retries_) + 1u)) + 50u);
    if (!waitDone(guard_timeout_ms)) {
        last_error_ = Error::DriverTimeout;
        return false;
    }

    const uint32_t status = readReg(status_reg_);
    last_hw_status_ = status;

    if ((status & STATUS_SUCCESS) == 0u) {
        setLastErrorFromStatus(status);
    } else {
        last_error_ = Error::None;
    }

    uint16_t count = static_cast<uint16_t>(readReg(rx_len_reg_) & 0x1FFu);
    if (count > kMaxFrameBytes) {
        count = kMaxFrameBytes;
    }

    for (uint16_t i = 0; i < count; ++i) {
        resp_frame[i] = static_cast<uint8_t>(readReg(rx_data_reg_) & 0xFFu);
    }
    *resp_len = count;

    if (last_error_ == Error::HwException && count >= 5u) {
        last_exception_code_ = resp_frame[2];
    }

    if (count >= 2u) {
        if (((slave != 0u) && (resp_frame[0] != slave)) || ((resp_frame[1] & 0x7Fu) != function)) {
            last_error_ = Error::ResponseMismatch;
        }
    }

    return (last_error_ == Error::None);
}

bool ModbusMaster::waitDone(uint32_t guard_timeout_ms) {
    const uint32_t start_ms = millis();
    while (true) {
        const uint32_t st = readReg(status_reg_);
        if ((st & STATUS_DONE) != 0u) {
            return true;
        }

        const uint32_t now = millis();
        if (static_cast<int32_t>(now - start_ms - guard_timeout_ms) >= 0) {
            return false;
        }

        __asm__ volatile("nop" ::: "memory");
    }
}

void ModbusMaster::setLastErrorFromStatus(uint32_t status) {
    if ((status & STATUS_TIMEOUT) != 0u) {
        last_error_ = Error::HwTimeout;
        return;
    }
    if ((status & STATUS_CRC_ERROR) != 0u) {
        last_error_ = Error::HwCrc;
        return;
    }
    if ((status & STATUS_FRAME_ERROR) != 0u) {
        last_error_ = Error::HwFrame;
        return;
    }
    if ((status & STATUS_EXCEPTION) != 0u) {
        last_error_ = Error::HwException;
        return;
    }
    if ((status & STATUS_RX_OVERFLOW) != 0u) {
        last_error_ = Error::HwOverflow;
        return;
    }
    if ((status & STATUS_UART_FRAME_ERROR) != 0u) {
        last_error_ = Error::HwUartFrame;
        return;
    }

    last_error_ = Error::HwUnknown;
}

bool ModbusMaster::readBitFunction(uint8_t slave,
                                   uint8_t function,
                                   uint16_t start_addr,
                                   uint16_t quantity,
                                   bool* out_values) {
    if (quantity == 0u || quantity > 2000u || out_values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[4];
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);

    uint8_t resp[kMaxFrameBytes] = {};
    uint16_t resp_len = 0u;
    if (!runTransaction(slave, function, req, sizeof(req), resp, &resp_len)) {
        return false;
    }

    const uint16_t expected_byte_count = static_cast<uint16_t>((quantity + 7u) / 8u);
    const uint16_t expected_len = static_cast<uint16_t>(5u + expected_byte_count);
    if (resp_len != expected_len) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if (resp[2] != static_cast<uint8_t>(expected_byte_count)) {
        last_error_ = Error::ResponseFormat;
        return false;
    }

    for (uint16_t i = 0; i < quantity; ++i) {
        const uint16_t byte_idx = static_cast<uint16_t>(3u + (i / 8u));
        const uint8_t bit_mask = static_cast<uint8_t>(1u << (i & 0x7u));
        out_values[i] = (resp[byte_idx] & bit_mask) != 0u;
    }

    last_error_ = Error::None;
    return true;
}

bool ModbusMaster::readRegsFunction(uint8_t slave,
                                    uint8_t function,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    uint16_t* out_values) {
    if (quantity == 0u || quantity > 125u || out_values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[4];
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);

    uint8_t resp[kMaxFrameBytes] = {};
    uint16_t resp_len = 0u;
    if (!runTransaction(slave, function, req, sizeof(req), resp, &resp_len)) {
        return false;
    }

    const uint16_t expected_byte_count = static_cast<uint16_t>(quantity * 2u);
    const uint16_t expected_len = static_cast<uint16_t>(5u + expected_byte_count);
    if (resp_len != expected_len) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if (resp[2] != static_cast<uint8_t>(expected_byte_count)) {
        last_error_ = Error::ResponseFormat;
        return false;
    }

    for (uint16_t i = 0; i < quantity; ++i) {
        const uint16_t idx = static_cast<uint16_t>(3u + i * 2u);
        out_values[i] = static_cast<uint16_t>((static_cast<uint16_t>(resp[idx]) << 8) |
                                              static_cast<uint16_t>(resp[idx + 1u]));
    }

    last_error_ = Error::None;
    return true;
}
