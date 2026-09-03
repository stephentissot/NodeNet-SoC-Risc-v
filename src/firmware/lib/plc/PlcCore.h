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

    // Binds the point catalog, Modbus master, and optional logger used by the PLC service.
    void begin(PointCatalog* point_catalog, ModbusMaster* modbus0, NodeLogger* logger = nullptr);

    // Attaches the shared PLC runtime publisher used to mirror point data into SDRAM.
    void attachRuntimePublisher(const PlcRuntimePublisherV1* publisher);

    // Sets the maximum address gap allowed when packing Modbus points into one batch.
    void setModbusBatchMaxGap(uint16_t max_gap);

    // Runs one non-blocking PLC service iteration: consume writes, schedule polls, publish runtime state.
    void loop();

    // Returns true while an asynchronous Modbus transaction is still in flight.
    bool pollTransactionActive() const;

private:
    static constexpr size_t kModbusRegisterBufferSize = 2u;
    static constexpr size_t kMaxModbusPollBatches = PointCatalog::kMaxPoints;
    static constexpr size_t kMaxModbusBatchMembers = PointCatalog::kMaxPoints;
    static constexpr uint16_t kMaxModbusBatchRegisters = 125u;
    static constexpr uint16_t kMaxModbusBatchBits = 256u;
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

    enum class PollState : uint8_t {
        Idle = 0,
        WaitingBatchResult
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
    PollState poll_state_ = PollState::Idle;
    ModbusPollBatch active_batch_ = {};
        uint32_t active_batch_started_ms_ = 0u;
    uint32_t active_batch_last_poll_ms_ = 0u;
    uint32_t active_batch_max_poll_gap_ms_ = 0u;
    uint32_t active_batch_poll_calls_ = 0u;
    bool active_bit_values_[kMaxModbusBatchBits] = {};
    uint16_t active_register_values_[kMaxModbusBatchRegisters] = {};

    // Rebuilds the Modbus poll plan only when the catalog-derived hash changes.
    void rebuildPollPlanIfNeeded();

    // Recomputes grouped Modbus poll batches from the current point catalog.
    void rebuildPollPlan();

    // Hashes the Modbus-facing catalog state to detect when batching must be rebuilt.
    uint32_t computeModbusPlanHash() const;

    // Advances the Modbus polling state machine by at most one step.
    void pollNextPoint();
    bool batchPollUrgency(const ModbusPollBatch& batch, uint32_t now_ms, uint32_t& urgency_out) const;
    bool startBatchPoll(const ModbusPollBatch& batch);
    bool completeActiveBatch(uint32_t now_ms);
    void failActiveBatch(uint32_t now_ms, PointQuality batch_error);
    bool isBatchDue(const ModbusPollBatch& batch, uint32_t now_ms) const;

    // Pushes CPU-owned point states into the shared PLC runtime windows.
    void syncRuntimeSnapshot(uint32_t now_ms);

    // Consumes PLC VM writes from shared runtime memory and applies them to the catalog.
    void consumeRuntimeWrites(uint32_t now_ms);
    bool consumeRuntimeWriteIndex(uint16_t runtime_index, uint32_t now_ms);
    bool decodeBitState(const PointDefinition& definition, bool bit_value, PointState& state) const;
    bool decodeRegisterState(const PointDefinition& definition,
                             const uint16_t* regs,
                             uint16_t available_regs,
                             PointState& state) const;
    bool commitRuntimeBool(uint16_t runtime_index, bool value, uint32_t now_ms);
    bool commitRuntimeUint16(uint16_t runtime_index, uint16_t value, uint32_t now_ms);
    bool commitRuntimeInt16(uint16_t runtime_index, int16_t value, uint32_t now_ms);
    bool commitRuntimeUint32(uint16_t runtime_index, uint32_t value, uint32_t now_ms);
    bool commitRuntimeInt32(uint16_t runtime_index, int32_t value, uint32_t now_ms);
    bool commitRuntimeFloat(uint16_t runtime_index, float value, uint32_t now_ms);
    bool commitRuntimeEnum(uint16_t runtime_index, int32_t value, uint32_t now_ms);
    PointQuality qualityFromModbusError(ModbusMaster::Error error) const;
};

#endif