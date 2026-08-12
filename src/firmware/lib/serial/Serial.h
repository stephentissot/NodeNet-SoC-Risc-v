#ifndef SERIAL_H
#define SERIAL_H

#include <cstddef>
#include <cstdint>

class Serial {
public:
    static constexpr uint32_t kDefaultClockHz = 25000000u;

    explicit Serial(uint32_t base_addr, uint32_t clock_hz = kDefaultClockHz);

    void begin(uint32_t baudrate);
    void begin(uint32_t baudrate, uint32_t clock_hz);

    int available() const;
    int availableForWrite() const;

    int read();
    int peek();

    std::size_t write(uint8_t c);
    std::size_t write(const uint8_t* data, std::size_t len);
    std::size_t write(const char* text);

    std::size_t print(const char* text);
    std::size_t println(const char* text);
    std::size_t println();

    void flush() const;

    bool hasOverrunError() const;
    bool hasFrameError() const;
    void clearErrors();

private:
    static constexpr uint32_t REG_DATA = 0x00u;
    static constexpr uint32_t REG_STATUS = 0x04u;
    static constexpr uint32_t REG_BAUD = 0x08u;

    static constexpr uint32_t STATUS_RX_EMPTY = (1u << 0);
    static constexpr uint32_t STATUS_RX_FULL = (1u << 1);
    static constexpr uint32_t STATUS_TX_EMPTY = (1u << 2);
    static constexpr uint32_t STATUS_TX_FULL = (1u << 3);
    static constexpr uint32_t STATUS_RX_OVERRUN = (1u << 4);
    static constexpr uint32_t STATUS_RX_FRAME = (1u << 5);

    static constexpr uint32_t CLEAR_RX_OVERRUN = (1u << 4);
    static constexpr uint32_t CLEAR_RX_FRAME = (1u << 5);

    static constexpr uint32_t kAssumedFifoDepth = 16u;
    static constexpr uint32_t kWriteTimeoutLoops = 200000u;
    static constexpr uint32_t kFlushTimeoutLoops = 500000u;

    volatile uint32_t* const data_reg_;
    volatile uint32_t* const status_reg_;
    volatile uint32_t* const baud_reg_;

    uint32_t clock_hz_;

    mutable bool has_peeked_;
    mutable uint8_t peeked_byte_;

    uint16_t computePrescale(uint32_t baudrate, uint32_t clock_hz) const;
    bool waitTxSpace() const;
    bool waitTxIdle() const;
    uint32_t status() const;
};

#endif
