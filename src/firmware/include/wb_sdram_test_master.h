#ifndef WB_SDRAM_TEST_MASTER_H
#define WB_SDRAM_TEST_MASTER_H

#include <cstdint>

class WbSdramTestMaster {
public:
    static constexpr uint32_t kDefaultBase = 0x10003000u;
    static constexpr uint32_t kClockKhz = 25000u;

    enum ConfigBits : uint32_t {
        kCfgContinuous = 1u << 0,
        kCfgWrite = 1u << 1,
        kCfgAlternateAddr = 1u << 2,
        kCfgCompare = 1u << 3,
    };

    enum CommandBits : uint32_t {
        kCmdStart = 1u << 0,
        kCmdStop = 1u << 1,
        kCmdClearCounters = 1u << 2,
    };

    struct ArbSnapshot {
        uint8_t state;
        bool lastGrantWasM1;
        bool rrPreferM1;
        bool m0Req;
        bool m1Req;
        uint32_t m0Grants;
        uint32_t m1Grants;
        uint32_t m0Stalls;
        uint32_t m1Stalls;
        uint32_t ackCount;
    };

    explicit WbSdramTestMaster(uint32_t base = kDefaultBase) : base_(base) {}

    void configure(bool continuous, bool write, bool alternateAddr, bool compare) const
    {
        uint32_t value = 0u;
        if (continuous) {
            value |= kCfgContinuous;
        }
        if (write) {
            value |= kCfgWrite;
        }
        if (alternateAddr) {
            value |= kCfgAlternateAddr;
        }
        if (compare) {
            value |= kCfgCompare;
        }
        writeReg(kRegConfig, value);
    }

    void setAddress0(uint32_t value) const { writeReg(kRegAddr0, value); }
    void setAddress1(uint32_t value) const { writeReg(kRegAddr1, value); }
    void setWriteData0(uint32_t value) const { writeReg(kRegWdata0, value); }
    void setWriteData1(uint32_t value) const { writeReg(kRegWdata1, value); }
    void setExpect0(uint32_t value) const { writeReg(kRegExpect0, value); }
    void setExpect1(uint32_t value) const { writeReg(kRegExpect1, value); }
    void setIntervalCycles(uint32_t value) const { writeReg(kRegInterval, value); }

    void setIntervalMs(uint32_t valueMs) const
    {
        const uint64_t cycles = static_cast<uint64_t>(valueMs) * static_cast<uint64_t>(kClockKhz);
        writeReg(kRegInterval, static_cast<uint32_t>(cycles & 0xFFFFFFFFu));
    }

    void start() const { writeReg(kRegCommand, kCmdStart); }
    void stop() const { writeReg(kRegCommand, kCmdStop); }
    void clearCounters() const { writeReg(kRegCommand, kCmdClearCounters); }

    uint32_t statusRaw() const { return readReg(kRegStatus); }
    bool busy() const { return (statusRaw() & (1u << 0)) != 0u; }
    bool running() const { return (statusRaw() & (1u << 1)) != 0u; }
    bool startPending() const { return (statusRaw() & (1u << 2)) != 0u; }
    bool gapPending() const { return (statusRaw() & (1u << 3)) != 0u; }
    bool mismatchSticky() const { return (statusRaw() & (1u << 6)) != 0u; }
    bool compareFailedLast() const { return (statusRaw() & (1u << 7)) != 0u; }
    bool active() const { return busy() || running() || startPending() || gapPending(); }

    uint32_t readback() const { return readReg(kRegReadback); }
    uint32_t issuedCount() const { return readReg(kRegIssued); }
    uint32_t ackedCount() const { return readReg(kRegAcked); }
    uint32_t mismatchCount() const { return readReg(kRegMismatch); }

    ArbSnapshot arbSnapshot() const
    {
        const uint32_t raw = readReg(kRegArbState);
        ArbSnapshot snapshot = {};
        snapshot.state = static_cast<uint8_t>(raw & 0x7u);
        snapshot.lastGrantWasM1 = ((raw >> 3) & 0x1u) != 0u;
        snapshot.rrPreferM1 = ((raw >> 4) & 0x1u) != 0u;
        snapshot.m0Req = ((raw >> 5) & 0x1u) != 0u;
        snapshot.m1Req = ((raw >> 6) & 0x1u) != 0u;
        snapshot.m0Grants = readReg(kRegArbGrant0);
        snapshot.m1Grants = readReg(kRegArbGrant1);
        snapshot.m0Stalls = readReg(kRegArbStall0);
        snapshot.m1Stalls = readReg(kRegArbStall1);
        snapshot.ackCount = readReg(kRegArbAckCount);
        return snapshot;
    }

private:
    static constexpr uint32_t kRegConfig = 0x00u;
    static constexpr uint32_t kRegCommand = 0x04u;
    static constexpr uint32_t kRegStatus = 0x08u;
    static constexpr uint32_t kRegAddr0 = 0x0Cu;
    static constexpr uint32_t kRegAddr1 = 0x10u;
    static constexpr uint32_t kRegWdata0 = 0x14u;
    static constexpr uint32_t kRegWdata1 = 0x18u;
    static constexpr uint32_t kRegExpect0 = 0x1Cu;
    static constexpr uint32_t kRegExpect1 = 0x20u;
    static constexpr uint32_t kRegReadback = 0x24u;
    static constexpr uint32_t kRegInterval = 0x28u;
    static constexpr uint32_t kRegIssued = 0x2Cu;
    static constexpr uint32_t kRegAcked = 0x30u;
    static constexpr uint32_t kRegMismatch = 0x34u;
    static constexpr uint32_t kRegArbState = 0x38u;
    static constexpr uint32_t kRegArbGrant0 = 0x3Cu;
    static constexpr uint32_t kRegArbGrant1 = 0x40u;
    static constexpr uint32_t kRegArbStall0 = 0x44u;
    static constexpr uint32_t kRegArbStall1 = 0x48u;
    static constexpr uint32_t kRegArbAckCount = 0x4Cu;

    volatile uint32_t& reg(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
    }

    uint32_t readReg(uint32_t offset) const
    {
        return reg(offset);
    }

    void writeReg(uint32_t offset, uint32_t value) const
    {
        reg(offset) = value;
    }

    uint32_t base_;
};

#endif