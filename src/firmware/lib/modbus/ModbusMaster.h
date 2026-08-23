#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <cstddef>
#include <cstdint>

#include "ModbusTypes.h"

class ModbusMaster {
public:
    static constexpr uint32_t kDefaultClockHz = 25000000u;
    static constexpr uint32_t kDefaultBaudrate = 9600u;
    static constexpr uint32_t kDefaultTimeoutMs = 200u;
    static constexpr uint8_t kDefaultRetries = 1u;
    static constexpr uint8_t kDefaultInterframeCharsQ1 = 14u;

    enum class Error : uint8_t {
        None = 0,
        Busy,
        InvalidArg,
        HwTimeout,
        HwCrc,
        HwFrame,
        HwException,
        HwOverflow,
        HwUartFrame,
        HwUnknown,
        ResponseMismatch,
        ResponseLength,
        ResponseFormat,
        DriverTimeout
    };

    explicit ModbusMaster(uint32_t base_addr, uint32_t clock_hz = kDefaultClockHz);

    void begin(uint32_t baudrate = kDefaultBaudrate,
               uint32_t timeout_ms = kDefaultTimeoutMs,
               uint8_t retries = kDefaultRetries,
               uint8_t interFrameCharsQ1 = kDefaultInterframeCharsQ1);

    void setTimeoutMs(uint32_t timeout_ms);
    void setRetries(uint8_t retries);
    void setInterframeCharsQ1(uint8_t chars_q1);

    bool readCoils(uint8_t slave, uint16_t start_addr, uint16_t quantity, bool* out_values);
    bool readDiscreteInputs(uint8_t slave, uint16_t start_addr, uint16_t quantity, bool* out_values);
    bool readHoldingRegisters(uint8_t slave, uint16_t start_addr, uint16_t quantity, uint16_t* out_values);
    bool readInputRegisters(uint8_t slave, uint16_t start_addr, uint16_t quantity, uint16_t* out_values);
    bool writeSingleCoil(uint8_t slave, uint16_t coil_addr, bool value);
    bool writeSingleCoilRaw(uint8_t slave, uint16_t coil_addr, uint16_t raw_value);
    bool writeSingleRegister(uint8_t slave, uint16_t reg_addr, uint16_t value);
    bool writeMultipleCoils(uint8_t slave,
                            uint16_t start_addr,
                            uint16_t quantity,
                            const bool* values);
    bool writeMultipleRegisters(uint8_t slave,
                                uint16_t start_addr,
                                uint16_t quantity,
                                const uint16_t* values);

    Error lastError() const;
    uint8_t lastExceptionCode() const;
    uint32_t lastHwStatus() const;

    static uint16_t regsToU16(uint16_t reg);
    static int16_t regsToI16(uint16_t reg);
    static uint32_t regsToU32(uint16_t high_reg, uint16_t low_reg);
    static uint32_t regsToU32ABCD(uint16_t high_reg, uint16_t low_reg);
    static uint32_t regsToU32BADC(uint16_t high_reg, uint16_t low_reg);
    static uint32_t regsToU32CDAB(uint16_t high_reg, uint16_t low_reg);
    static uint32_t regsToU32DCBA(uint16_t high_reg, uint16_t low_reg);
    static int32_t regsToI32(uint16_t high_reg, uint16_t low_reg);
    static float regsToFloatABCD(uint16_t high_reg, uint16_t low_reg);
    static float regsToFloatBADC(uint16_t high_reg, uint16_t low_reg);
    static float regsToFloatCDAB(uint16_t high_reg, uint16_t low_reg);
    static float regsToFloatDCBA(uint16_t high_reg, uint16_t low_reg);

private:
    static constexpr uint32_t REG_CONTROL = 0x00u;
    static constexpr uint32_t REG_STATUS = 0x04u;
    static constexpr uint32_t REG_UART_DIV = 0x08u;
    static constexpr uint32_t REG_SLAVE_FUNC = 0x0Cu;
    static constexpr uint32_t REG_TIMEOUT = 0x10u;
    static constexpr uint32_t REG_INTERFRAME = 0x14u;
    static constexpr uint32_t REG_RETRY = 0x18u;
    static constexpr uint32_t REG_TX_LEN = 0x1Cu;
    static constexpr uint32_t REG_TX_DATA = 0x20u;
    static constexpr uint32_t REG_RX_LEN = 0x24u;
    static constexpr uint32_t REG_RX_DATA = 0x28u;

    static constexpr uint32_t CTRL_START = (1u << 0);
    static constexpr uint32_t CTRL_CLEAR_STATUS = (1u << 1);

    static constexpr uint32_t STATUS_BUSY = (1u << 0);
    static constexpr uint32_t STATUS_DONE = (1u << 1);
    static constexpr uint32_t STATUS_SUCCESS = (1u << 2);
    static constexpr uint32_t STATUS_TIMEOUT = (1u << 3);
    static constexpr uint32_t STATUS_CRC_ERROR = (1u << 4);
    static constexpr uint32_t STATUS_FRAME_ERROR = (1u << 5);
    static constexpr uint32_t STATUS_EXCEPTION = (1u << 6);
    static constexpr uint32_t STATUS_RX_OVERFLOW = (1u << 7);
    static constexpr uint32_t STATUS_UART_FRAME_ERROR = (1u << 8);

    static constexpr uint16_t kMaxDataBytes = 252u;
    static constexpr uint16_t kMaxFrameBytes = 256u;

    volatile uint32_t* const control_reg_;
    volatile uint32_t* const status_reg_;
    volatile uint32_t* const uart_div_reg_;
    volatile uint32_t* const slave_func_reg_;
    volatile uint32_t* const timeout_reg_;
    volatile uint32_t* const interframe_reg_;
    volatile uint32_t* const retry_reg_;
    volatile uint32_t* const tx_len_reg_;
    volatile uint32_t* const tx_data_reg_;
    volatile uint32_t* const rx_len_reg_;
    volatile uint32_t* const rx_data_reg_;

    uint32_t clock_hz_;
    uint32_t timeout_ms_;
    uint8_t retries_;

    Error last_error_;
    uint8_t last_exception_code_;
    uint32_t last_hw_status_;

    uint32_t readReg(volatile uint32_t* reg) const;
    void writeReg(volatile uint32_t* reg, uint32_t value) const;

    uint32_t computeDivisor(uint32_t baudrate) const;
    uint32_t computeTimeoutCycles(uint32_t timeout_ms) const;
    uint32_t computeInterframeCycles(uint32_t baudrate) const;
    uint32_t computeInterframeCyclesCharsQ1(uint32_t baudrate, uint8_t chars_q1) const;

    bool runTransaction(uint8_t slave,
                        uint8_t function,
                        const uint8_t* req_data,
                        uint8_t req_len,
                        uint8_t* resp_frame,
                        uint16_t* resp_len);
    bool readBitFunction(uint8_t slave,
                         uint8_t function,
                         uint16_t start_addr,
                         uint16_t quantity,
                         bool* out_values);
    bool readRegsFunction(uint8_t slave,
                          uint8_t function,
                          uint16_t start_addr,
                          uint16_t quantity,
                          uint16_t* out_values);

    bool waitDone(uint32_t guard_timeout_ms);
    void setLastErrorFromStatus(uint32_t status);
};

#endif
