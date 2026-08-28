#ifndef PLC_CORE_H
#define PLC_CORE_H

#include <cstddef>
#include <cstdint>

#include "ModbusMaster.h"
#include "PointCatalog.h"

class NodeLogger;
class PlcRuntimePublisherV1;

class PlcCore {
public:
    PlcCore() = default;

    void begin(PointCatalog* point_catalog, ModbusMaster* modbus0, NodeLogger* logger = nullptr);
    void attachRuntimePublisher(const PlcRuntimePublisherV1* publisher);
    void setModbusBatchMaxGap(uint16_t max_gap);
    void loop();

private:
    static constexpr size_t kModbusRegisterBufferSize = 2u;
    static constexpr size_t kMaxModbusPollBatches = PointCatalog::kMaxPoints;
    static constexpr size_t kMaxModbusBatchMembers = PointCatalog::kMaxPoints;
    static constexpr uint16_t kMaxModbusBatchRegisters = 125u;
    static constexpr uint16_t kMaxModbusBatchBits = 256u;
    static constexpr uint32_t kVmScanPeriodMs = 50u;

    struct ModbusPollBatchMember {
        uint16_t catalog_index = 0u;
        uint16_t address_offset = 0u;
    };

    struct ModbusPollBatch {
        bool valid = false;
        uint8_t port_index = 0u;
        uint8_t slave_address = 0u;
        ModbusTable table = ModbusTable::HoldingRegisters;
        uint16_t start_address = 0u;
        uint16_t quantity = 0u;
        uint16_t member_start = 0u;
        uint16_t member_count = 0u;
    };

    PointCatalog* point_catalog_ = nullptr;
    ModbusMaster* modbus0_ = nullptr;
    NodeLogger* logger_ = nullptr;
    const PlcRuntimePublisherV1* runtime_publisher_ = nullptr;
    ModbusPollBatch batches_[kMaxModbusPollBatches] = {};
    ModbusPollBatchMember batch_members_[kMaxModbusBatchMembers] = {};
    size_t batch_count_ = 0u;
    size_t next_batch_index_ = 0u;
    uint32_t modbus_plan_hash_ = 0u;
    uint16_t modbus_batch_max_gap_ = 6u;

    void rebuildPollPlanIfNeeded();
    void rebuildPollPlan();
    uint32_t computeModbusPlanHash() const;
    void pollNextPoint();
    bool pollBatch(const ModbusPollBatch& batch, uint32_t now_ms);
    bool isBatchDue(const ModbusPollBatch& batch, uint32_t now_ms) const;
    void syncRuntimeSnapshot(uint32_t now_ms);
    void consumeRuntimeWrites(uint32_t now_ms);
    bool readBatchBits(const ModbusPollBatch& batch, bool* bit_values);
    bool readBatchRegisters(const ModbusPollBatch& batch, uint16_t* regs_out);
    bool decodeBitState(const PointDefinition& definition, bool bit_value, PointState& state) const;
    bool decodeRegisterState(const PointDefinition& definition,
                             const uint16_t* regs,
                             uint16_t available_regs,
                             PointState& state) const;
    bool readRuntimeBool(uint16_t runtime_index, bool& value_out) const;
    bool commitRuntimeBool(uint16_t runtime_index, bool value, uint32_t now_ms);
    bool readRuntimeInt16(uint16_t runtime_index, int16_t& value_out) const;
    bool commitRuntimeInt16(uint16_t runtime_index, int16_t value, uint32_t now_ms);
    PointQuality qualityFromModbusError(ModbusMaster::Error error) const;
};

#endif