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
            baudrate_(kDefaultBaudrate),
      timeout_ms_(kDefaultTimeoutMs),
      retries_(kDefaultRetries),
            interframe_chars_q1_(kDefaultInterframeCharsQ1),
      last_error_(Error::None),
      last_exception_code_(0u),
    last_hw_status_(0u),
    async_read_kind_(AsyncReadKind::None),
    async_slave_(0u),
    async_function_(0u),
    async_quantity_(0u),
    async_deadline_ms_(0u),
    async_active_(false),
    async_result_ready_(false),
        async_started_ms_(0u),
        async_first_rx_ms_(0u),
        async_last_rx_ms_(0u),
        async_observed_rx_len_(0u),
    async_response_{},
    async_response_len_(0u) {
}

void ModbusMaster::begin(uint32_t baudrate, uint32_t timeout_ms, uint8_t retries, uint8_t interFrameCharsQ1) {
        baudrate_ = (baudrate == 0u) ? 1u : baudrate;
    timeout_ms_ = timeout_ms;
    retries_ = retries;
        interframe_chars_q1_ = (interFrameCharsQ1 == 0u) ? 1u : interFrameCharsQ1;

        writeReg(uart_div_reg_, computeDivisor(baudrate_));
    writeReg(timeout_reg_, computeTimeoutCycles(timeout_ms_));
        writeReg(interframe_reg_, computeInterframeCycles(baudrate_));
    writeReg(retry_reg_, retries_);
    writeReg(control_reg_, CTRL_CLEAR_STATUS);

        setInterframeCharsQ1(interframe_chars_q1_);

    last_error_ = Error::None;
    last_exception_code_ = 0u;
    last_hw_status_ = 0u;
    clearAsyncTransaction();
}

void ModbusMaster::setBaudrate(uint32_t baudrate) {
    baudrate_ = (baudrate == 0u) ? 1u : baudrate;
    writeReg(uart_div_reg_, computeDivisor(baudrate_));
    writeReg(interframe_reg_, computeInterframeCyclesCharsQ1(baudrate_, interframe_chars_q1_));
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
    interframe_chars_q1_ = (chars_q1 == 0u) ? 1u : chars_q1;
    writeReg(interframe_reg_, computeInterframeCyclesCharsQ1(baudrate_, interframe_chars_q1_));
}

bool ModbusMaster::readCoils(uint8_t slave, uint16_t start_addr, uint16_t quantity, bool* out_values) {
    return readBitFunction(slave, 0x01u, start_addr, quantity, out_values);
}

bool ModbusMaster::startReadCoils(uint8_t slave, uint16_t start_addr, uint16_t quantity) {
    if (quantity == 0u || quantity > 2000u) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[4];
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);
    return startTransactionAsync(slave, 0x01u, req, sizeof(req), AsyncReadKind::Bits, quantity);
}

bool ModbusMaster::readDiscreteInputs(uint8_t slave,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      bool* out_values) {
    return readBitFunction(slave, 0x02u, start_addr, quantity, out_values);
}

bool ModbusMaster::startReadDiscreteInputs(uint8_t slave,
                                           uint16_t start_addr,
                                           uint16_t quantity) {
    if (quantity == 0u || quantity > 2000u) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[4];
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);
    return startTransactionAsync(slave, 0x02u, req, sizeof(req), AsyncReadKind::Bits, quantity);
}

bool ModbusMaster::readHoldingRegisters(uint8_t slave,
                                        uint16_t start_addr,
                                        uint16_t quantity,
                                        uint16_t* out_values) {
    return readRegsFunction(slave, 0x03u, start_addr, quantity, out_values);
}

bool ModbusMaster::startReadHoldingRegisters(uint8_t slave,
                                             uint16_t start_addr,
                                             uint16_t quantity) {
    if (quantity == 0u || quantity > 125u) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[4];
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);
    return startTransactionAsync(slave, 0x03u, req, sizeof(req), AsyncReadKind::Registers, quantity);
}

bool ModbusMaster::readInputRegisters(uint8_t slave,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      uint16_t* out_values) {
    return readRegsFunction(slave, 0x04u, start_addr, quantity, out_values);
}

bool ModbusMaster::startReadInputRegisters(uint8_t slave,
                                           uint16_t start_addr,
                                           uint16_t quantity) {
    if (quantity == 0u || quantity > 125u) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    uint8_t req[4];
    req[0] = static_cast<uint8_t>((start_addr >> 8) & 0xFFu);
    req[1] = static_cast<uint8_t>(start_addr & 0xFFu);
    req[2] = static_cast<uint8_t>((quantity >> 8) & 0xFFu);
    req[3] = static_cast<uint8_t>(quantity & 0xFFu);
    return startTransactionAsync(slave, 0x04u, req, sizeof(req), AsyncReadKind::Registers, quantity);
}

ModbusMaster::TransactionStatus ModbusMaster::pollTransaction() {
    if (async_result_ready_) {
        return TransactionStatus::Success;
    }

    if (!async_active_) {
        return TransactionStatus::Idle;
    }

    const uint32_t status = readReg(status_reg_);
    if ((status & STATUS_DONE) == 0u) {
        const uint32_t now_ms = millis();
            const uint16_t observed_rx_len = static_cast<uint16_t>(readReg(rx_len_reg_) & 0x1FFu);
            if (observed_rx_len > async_observed_rx_len_) {
                async_observed_rx_len_ = observed_rx_len;
                async_last_rx_ms_ = now_ms;
                if (async_first_rx_ms_ == 0u) {
                    async_first_rx_ms_ = now_ms;
                }
            }
        if (static_cast<int32_t>(now_ms - async_deadline_ms_) >= 0) {
            abortTransaction();
            last_error_ = Error::DriverTimeout;
            last_hw_status_ = status;
            return TransactionStatus::WatchdogTimeout;
        }
        return TransactionStatus::Busy;
    }

    last_hw_status_ = status;
    captureResponseFrame();
    async_active_ = false;

    if ((status & STATUS_SUCCESS) == 0u) {
        setLastErrorFromStatus(status);
        if (last_error_ == Error::HwException && async_response_len_ >= 5u) {
            last_exception_code_ = async_response_[2];
        }
        clearAsyncTransaction();
        return TransactionStatus::Error;
    }

    last_error_ = Error::None;
    async_result_ready_ = true;
    return TransactionStatus::Success;
}

bool ModbusMaster::finishReadBits(bool* out_values, uint16_t quantity) {
    if (!async_result_ready_ || async_read_kind_ != AsyncReadKind::Bits || out_values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const bool ok = decodeBitResponse(async_function_, quantity, async_response_, async_response_len_, out_values);
    clearAsyncTransaction();
    return ok;
}

bool ModbusMaster::finishReadRegisters(uint16_t* out_values, uint16_t quantity) {
    if (!async_result_ready_ || async_read_kind_ != AsyncReadKind::Registers || out_values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const bool ok = decodeRegisterResponse(async_function_, quantity, async_response_, async_response_len_, out_values);
    clearAsyncTransaction();
    return ok;
}

bool ModbusMaster::transactionActive() const {
    return async_active_ || async_result_ready_;
}

void ModbusMaster::abortTransaction() {
    writeReg(control_reg_, CTRL_CLEAR_STATUS | CTRL_ABORT);
    clearAsyncTransaction();
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

uint32_t ModbusMaster::debugBaudrate() const {
    return baudrate_;
}

uint32_t ModbusMaster::debugTimeoutMs() const {
    return timeout_ms_;
}

uint8_t ModbusMaster::debugRetries() const {
    return retries_;
}

uint8_t ModbusMaster::debugInterframeCharsQ1() const {
    return interframe_chars_q1_;
}

uint32_t ModbusMaster::debugAsyncStartedMs() const {
    return async_started_ms_;
}

uint32_t ModbusMaster::debugAsyncFirstRxMs() const {
    return async_first_rx_ms_;
}

uint32_t ModbusMaster::debugAsyncLastRxMs() const {
    return async_last_rx_ms_;
}

uint16_t ModbusMaster::debugAsyncObservedRxLen() const {
    return async_observed_rx_len_;
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

bool ModbusMaster::startTransactionAsync(uint8_t slave,
                                         uint8_t function,
                                         const uint8_t* req_data,
                                         uint8_t req_len,
                                         AsyncReadKind read_kind,
                                         uint16_t quantity) {
    if (req_data == nullptr || req_len > kMaxDataBytes) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    if (transactionActive()) {
        last_error_ = Error::Busy;
        return false;
    }

    const uint32_t status = readReg(status_reg_);
    if ((status & STATUS_BUSY) != 0u) {
        last_hw_status_ = status;
        last_error_ = Error::Busy;
        return false;
    }

    last_exception_code_ = 0u;
    last_hw_status_ = status;
    clearAsyncTransaction();

    writeReg(control_reg_, CTRL_CLEAR_STATUS);
    writeReg(retry_reg_, retries_);
    writeReg(slave_func_reg_, (static_cast<uint32_t>(slave) << 8) | static_cast<uint32_t>(function));
    writeReg(tx_len_reg_, static_cast<uint32_t>(req_len));
    for (uint8_t i = 0u; i < req_len; ++i) {
        writeReg(tx_data_reg_, static_cast<uint32_t>(req_data[i]));
    }
    writeReg(control_reg_, CTRL_START);

    async_read_kind_ = read_kind;
    async_slave_ = slave;
    async_function_ = function;
    async_quantity_ = quantity;
    async_active_ = true;
    async_result_ready_ = false;
    async_response_len_ = 0u;
    async_deadline_ms_ = millis() + static_cast<uint32_t>((timeout_ms_ * (static_cast<uint32_t>(retries_) + 1u)) + 100u);
                                            async_started_ms_ = millis();
                                            async_first_rx_ms_ = 0u;
                                            async_last_rx_ms_ = 0u;
                                            async_observed_rx_len_ = 0u;
    last_error_ = Error::None;
    return true;
}

void ModbusMaster::clearAsyncTransaction() {
    async_read_kind_ = AsyncReadKind::None;
    async_slave_ = 0u;
    async_function_ = 0u;
    async_quantity_ = 0u;
    async_deadline_ms_ = 0u;
    async_active_ = false;
    async_result_ready_ = false;
    async_response_len_ = 0u;
        async_started_ms_ = 0u;
        async_first_rx_ms_ = 0u;
        async_last_rx_ms_ = 0u;
        async_observed_rx_len_ = 0u;
}

void ModbusMaster::captureResponseFrame() {
    const uint16_t count = static_cast<uint16_t>(readReg(rx_len_reg_) & 0x1FFu);
    async_response_len_ = count > kMaxFrameBytes ? kMaxFrameBytes : count;
    writeReg(rx_len_reg_, 0u);
    for (uint16_t index = 0u; index < async_response_len_; ++index) {
        async_response_[index] = static_cast<uint8_t>(readReg(rx_data_reg_) & 0xFFu);
    }

    if (async_response_len_ >= 2u) {
        if (((async_slave_ != 0u) && (async_response_[0] != async_slave_)) ||
            ((async_response_[1] & 0x7Fu) != async_function_)) {
            last_error_ = Error::ResponseMismatch;
        }
    }
}

bool ModbusMaster::decodeBitResponse(uint8_t function,
                                     uint16_t quantity,
                                     const uint8_t* resp,
                                     uint16_t resp_len,
                                     bool* out_values) {
    if (quantity == 0u || quantity > 2000u || resp == nullptr || out_values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const uint16_t expected_byte_count = static_cast<uint16_t>((quantity + 7u) / 8u);
    const uint16_t expected_len = static_cast<uint16_t>(5u + expected_byte_count);
    if (resp_len != expected_len) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if ((resp[1] & 0x7Fu) != function || resp[2] != static_cast<uint8_t>(expected_byte_count)) {
        last_error_ = Error::ResponseFormat;
        return false;
    }

    for (uint16_t i = 0u; i < quantity; ++i) {
        const uint16_t byte_idx = static_cast<uint16_t>(3u + (i / 8u));
        const uint8_t bit_mask = static_cast<uint8_t>(1u << (i & 0x7u));
        out_values[i] = (resp[byte_idx] & bit_mask) != 0u;
    }

    last_error_ = Error::None;
    return true;
}

bool ModbusMaster::decodeRegisterResponse(uint8_t function,
                                          uint16_t quantity,
                                          const uint8_t* resp,
                                          uint16_t resp_len,
                                          uint16_t* out_values) {
    if (quantity == 0u || quantity > 125u || resp == nullptr || out_values == nullptr) {
        last_error_ = Error::InvalidArg;
        return false;
    }

    const uint16_t expected_byte_count = static_cast<uint16_t>(quantity * 2u);
    const uint16_t expected_len = static_cast<uint16_t>(5u + expected_byte_count);
    if (resp_len != expected_len) {
        last_error_ = Error::ResponseLength;
        return false;
    }

    if ((resp[1] & 0x7Fu) != function || resp[2] != static_cast<uint8_t>(expected_byte_count)) {
        last_error_ = Error::ResponseFormat;
        return false;
    }

    for (uint16_t i = 0u; i < quantity; ++i) {
        const uint16_t idx = static_cast<uint16_t>(3u + i * 2u);
        out_values[i] = static_cast<uint16_t>((static_cast<uint16_t>(resp[idx]) << 8) |
                                              static_cast<uint16_t>(resp[idx + 1u]));
    }

    last_error_ = Error::None;
    return true;
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

    return decodeBitResponse(function, quantity, resp, resp_len, out_values);
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

    return decodeRegisterResponse(function, quantity, resp, resp_len, out_values);
}
