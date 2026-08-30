#include "PlcCore.h"

#include <cstring>

#include "bigsister.h"
#include "nodenetLogger.h"
#include "plc_loader_v1.h"
#include "plc_runtime_abi.h"

namespace {

float apply_numeric_scale(float value, const PointDefinition& definition) {
    return value * definition.scale;
}

static bool modbus_table_is_bitwise(ModbusTable table)
{
    return table == ModbusTable::Coils || table == ModbusTable::DiscreteInputs;
}

static bool starts_with(const char* text, const char* prefix)
{
    return text != nullptr && prefix != nullptr && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

static bool is_dynamic_slot_variable_point(const PointDefinition& definition)
{
    if (!starts_with(definition.id.feature, "plc.slot")) {
        return false;
    }

    const char* feature = definition.id.feature + 8u;
    if (*feature < '0' || *feature > '9') {
        return false;
    }

    while (*feature >= '0' && *feature <= '9') {
        ++feature;
    }

    if (*feature != '\0') {
        return false;
    }

    static constexpr const char* kBuiltinSlotPointIds[] = {
        "loaded",
        "state",
        "runEnabled",
        "status",
        "cycleCounter",
        "faultCode",
        "faultInfo",
        "bytecodeSize",
        "source",
        "programType",
        "paramsSummary",
        "inputChannel",
        "outputChannel",
        "runtimeMapOk",
        "start",
        "stop",
        "reset",
        "clearFault",
    };

    for (const char* reserved : kBuiltinSlotPointIds) {
        if (std::strcmp(definition.id.point_id, reserved) == 0) {
            return false;
        }
    }

    return definition.id.point_id[0] != '\0';
}

static uint16_t modbus_point_span(const PointDefinition& definition)
{
    if (modbus_table_is_bitwise(definition.ref.modbus.table)) {
        return 1u;
    }

    return definition.ref.modbus.register_count == 0u ? 0u : definition.ref.modbus.register_count;
}

static bool modbus_poll_sort_before(const PointDefinition& lhs, size_t lhs_index,
                                    const PointDefinition& rhs, size_t rhs_index)
{
    if (lhs.ref.modbus.port_index != rhs.ref.modbus.port_index) {
        return lhs.ref.modbus.port_index < rhs.ref.modbus.port_index;
    }
    if (lhs.ref.modbus.slave_address != rhs.ref.modbus.slave_address) {
        return lhs.ref.modbus.slave_address < rhs.ref.modbus.slave_address;
    }
    if (lhs.ref.modbus.table != rhs.ref.modbus.table) {
        return static_cast<uint8_t>(lhs.ref.modbus.table) < static_cast<uint8_t>(rhs.ref.modbus.table);
    }
    if (lhs.ref.modbus.address != rhs.ref.modbus.address) {
        return lhs.ref.modbus.address < rhs.ref.modbus.address;
    }

    return lhs_index < rhs_index;
}

}

void PlcCore::begin(PointCatalog* point_catalog, ModbusMaster* modbus0, NodeLogger* logger) {
    point_catalog_ = point_catalog;
    modbus0_ = modbus0;
    logger_ = logger;
    batch_count_ = 0u;
    next_batch_index_ = 0u;
    modbus_plan_hash_ = 0u;
    poll_state_ = PollState::Idle;
    active_batch_ = {};
    active_batch_started_ms_ = 0u;
    active_batch_last_poll_ms_ = 0u;
    active_batch_max_poll_gap_ms_ = 0u;
    active_batch_poll_calls_ = 0u;
    std::memset(batches_, 0, sizeof(batches_));
    std::memset(batch_members_, 0, sizeof(batch_members_));
    std::memset(active_bit_values_, 0, sizeof(active_bit_values_));
    std::memset(active_register_values_, 0, sizeof(active_register_values_));
}

void PlcCore::attachRuntimePublisher(const PlcRuntimePublisherV1* publisher)
{
    runtime_publisher_ = publisher;
}

void PlcCore::setModbusBatchMaxGap(uint16_t max_gap)
{
    modbus_batch_max_gap_ = max_gap;
    modbus_plan_hash_ = 0u;
}

void PlcCore::loop() {
    if (point_catalog_ == nullptr) {
        return;
    }

    const uint32_t now_ms = millis();
    consumeRuntimeWrites(now_ms);
    if (poll_state_ == PollState::Idle) {
        rebuildPollPlanIfNeeded();
    }
    pollNextPoint();
    syncRuntimeSnapshot(now_ms);
}

bool PlcCore::pollTransactionActive() const
{
    return poll_state_ == PollState::WaitingBatchResult;
}

void PlcCore::syncRuntimeSnapshot(uint32_t now_ms)
{
    if (runtime_publisher_ == nullptr) {
        return;
    }

    (void)const_cast<PlcRuntimePublisherV1*>(runtime_publisher_)->publish(*point_catalog_, now_ms);
}

void PlcCore::consumeRuntimeWrites(uint32_t now_ms)
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr) {
        return;
    }

    uint16_t queued_runtime_index = PlcRuntimePublisherV1::kInvalidPointIndex;
    while (runtime_publisher_->popRuntimeWriteIndex(queued_runtime_index)) {
        (void)consumeRuntimeWriteIndex(queued_runtime_index, now_ms);
    }
}

bool PlcCore::consumeRuntimeWriteIndex(uint16_t runtime_index, uint32_t now_ms)
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr ||
        runtime_index == PlcRuntimePublisherV1::kInvalidPointIndex) {
        return false;
    }

    const size_t catalog_index = runtime_publisher_->catalogIndexForRuntimeIndex(runtime_index);
    if (catalog_index >= point_catalog_->size()) {
        return false;
    }

    volatile PlcPointValueV1* runtime_values = reinterpret_cast<volatile PlcPointValueV1*>(
        static_cast<uintptr_t>(kPlcRuntimeValueBase));
    volatile PlcPointStatusV1* runtime_statuses = reinterpret_cast<volatile PlcPointStatusV1*>(
        static_cast<uintptr_t>(kPlcRuntimeStatusBase));
    volatile const PlcPointStatusV1& runtime_status = runtime_statuses[runtime_index];
    if (runtime_status.last_writer != kPlcRuntimeWriterPlcVm) {
        return false;
    }

    const PointDefinition& definition = point_catalog_->entries()[catalog_index];
    const uint32_t effective_now_ms = runtime_status.last_update_ms != 0u
        ? runtime_status.last_update_ms
        : now_ms;
    bool consumed = false;

    switch (definition.value_type) {
    case PointValueType::Bool: {
        const bool value = (runtime_values[runtime_index].raw0 & 1u) != 0u;
        consumed = commitRuntimeBool(runtime_index, value, effective_now_ms);
        break;
    }

    case PointValueType::Int16: {
        const int16_t value = static_cast<int16_t>(runtime_values[runtime_index].raw0 & 0xFFFFu);
        consumed = commitRuntimeInt16(runtime_index, value, effective_now_ms);
        break;
    }

    default:
        break;
    }

    if (consumed) {
        runtime_statuses[runtime_index].last_writer = kPlcRuntimeWriterCpu;
    }

    return consumed;
}

uint32_t PlcCore::computeModbusPlanHash() const
{
    if (point_catalog_ == nullptr) {
        return 0u;
    }

    uint32_t hash = 2166136261u;
    auto append_bytes = [&hash](const void* data, size_t len) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
    };

    const PointDefinition* definitions = point_catalog_->entries();
    for (size_t index = 0; index < point_catalog_->size(); ++index) {
        const PointDefinition& definition = definitions[index];
        if (definition.backend != PointBackend::Modbus) {
            continue;
        }

        append_bytes(&definition.ref.modbus.port_index, sizeof(definition.ref.modbus.port_index));
        append_bytes(&definition.ref.modbus.slave_address, sizeof(definition.ref.modbus.slave_address));
        append_bytes(&definition.ref.modbus.address, sizeof(definition.ref.modbus.address));
        append_bytes(&definition.ref.modbus.register_count, sizeof(definition.ref.modbus.register_count));
        append_bytes(&definition.ref.modbus.table, sizeof(definition.ref.modbus.table));
        append_bytes(&definition.polling.refresh_ms, sizeof(definition.polling.refresh_ms));
        append_bytes(&definition.value_type, sizeof(definition.value_type));
    }

    append_bytes(&modbus_batch_max_gap_, sizeof(modbus_batch_max_gap_));

    return hash;
}

void PlcCore::rebuildPollPlanIfNeeded()
{
    const uint32_t next_hash = computeModbusPlanHash();
    if (next_hash == modbus_plan_hash_) {
        return;
    }

    rebuildPollPlan();
    modbus_plan_hash_ = next_hash;
}

void PlcCore::rebuildPollPlan()
{
    batch_count_ = 0u;
    next_batch_index_ = 0u;
    std::memset(batches_, 0, sizeof(batches_));
    std::memset(batch_members_, 0, sizeof(batch_members_));

    if (point_catalog_ == nullptr) {
        return;
    }

    const PointDefinition* definitions = point_catalog_->entries();
    size_t sorted_indices[PointCatalog::kMaxPoints] = {};
    size_t sorted_count = 0u;
    for (size_t index = 0; index < point_catalog_->size(); ++index) {
        const PointDefinition& definition = definitions[index];
        if (definition.backend != PointBackend::Modbus) {
            continue;
        }

        if (definition.ref.modbus.table == ModbusTable::Coils &&
            (definition.ref.modbus.access == ModbusAccess::Write ||
             definition.ref.modbus.access == ModbusAccess::ReadWrite)) {
            continue;
        }

        sorted_indices[sorted_count++] = index;
    }

    for (size_t i = 1u; i < sorted_count; ++i) {
        size_t current = sorted_indices[i];
        size_t insert = i;
        while (insert > 0u &&
               modbus_poll_sort_before(definitions[current],
                                       current,
                                       definitions[sorted_indices[insert - 1u]],
                                       sorted_indices[insert - 1u])) {
            sorted_indices[insert] = sorted_indices[insert - 1u];
            --insert;
        }
        sorted_indices[insert] = current;
    }

    uint16_t member_cursor = 0u;
    for (size_t sorted = 0u; sorted < sorted_count; ++sorted) {
        const size_t catalog_index = sorted_indices[sorted];
        const PointDefinition& definition = definitions[catalog_index];
        const uint16_t span = modbus_point_span(definition);
        if (span == 0u) {
            continue;
        }

        const bool is_bitwise = modbus_table_is_bitwise(definition.ref.modbus.table);
        const uint16_t batch_limit = is_bitwise ? kMaxModbusBatchBits : kMaxModbusBatchRegisters;
        const uint32_t point_end = static_cast<uint32_t>(definition.ref.modbus.address) + static_cast<uint32_t>(span) - 1u;

        bool append_to_batch = false;
        if (batch_count_ > 0u) {
            ModbusPollBatch& batch = batches_[batch_count_ - 1u];
            const uint32_t batch_end = static_cast<uint32_t>(batch.start_address) + static_cast<uint32_t>(batch.quantity) - 1u;
            const uint32_t merged_end = point_end > batch_end ? point_end : batch_end;
            const uint32_t merged_quantity = merged_end - static_cast<uint32_t>(batch.start_address) + 1u;
            const uint32_t max_next_address = batch_end + 1u + static_cast<uint32_t>(modbus_batch_max_gap_);
            append_to_batch = batch.valid &&
                              batch.port_index == definition.ref.modbus.port_index &&
                              batch.slave_address == definition.ref.modbus.slave_address &&
                              batch.table == definition.ref.modbus.table &&
                              static_cast<uint32_t>(definition.ref.modbus.address) <= max_next_address &&
                              merged_quantity <= batch_limit;
            if (append_to_batch) {
                batch.quantity = static_cast<uint16_t>(merged_quantity);
            }
        }

        if (!append_to_batch) {
            if (batch_count_ >= kMaxModbusPollBatches || member_cursor >= kMaxModbusBatchMembers) {
                break;
            }

            ModbusPollBatch& batch = batches_[batch_count_++];
            batch.valid = true;
            batch.port_index = definition.ref.modbus.port_index;
            batch.slave_address = definition.ref.modbus.slave_address;
            batch.table = definition.ref.modbus.table;
            batch.start_address = definition.ref.modbus.address;
            batch.quantity = span;
            batch.member_start = member_cursor;
            batch.member_count = 0u;
        }

        ModbusPollBatch& batch = batches_[batch_count_ - 1u];
        batch_members_[member_cursor].catalog_index = static_cast<uint16_t>(catalog_index);
        batch_members_[member_cursor].address_offset = static_cast<uint16_t>(definition.ref.modbus.address - batch.start_address);
        ++member_cursor;
        ++batch.member_count;
    }
}

bool PlcCore::readRuntimeBool(uint16_t runtime_index, bool& value_out) const
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr) {
        return false;
    }

    const size_t catalog_index = runtime_publisher_->catalogIndexForRuntimeIndex(runtime_index);
    if (catalog_index >= point_catalog_->size()) {
        return false;
    }

    const PointDefinition& definition = point_catalog_->entries()[catalog_index];
    const PointState& state = point_catalog_->states()[catalog_index];
    if (definition.value_type != PointValueType::Bool) {
        return false;
    }

    value_out = state.value.b;
    return true;
}

bool PlcCore::commitRuntimeBool(uint16_t runtime_index, bool value, uint32_t now_ms)
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr) {
        return false;
    }

    const size_t catalog_index = runtime_publisher_->catalogIndexForRuntimeIndex(runtime_index);
    if (catalog_index >= point_catalog_->size()) {
        return false;
    }

    const PointDefinition& definition = point_catalog_->entries()[catalog_index];
    const PointState& current_state = point_catalog_->states()[catalog_index];
    const bool internal_slot_variable = is_dynamic_slot_variable_point(definition);
    if (definition.value_type != PointValueType::Bool ||
        (!internal_slot_variable &&
         definition.direction != PointDirection::Output &&
         definition.direction != PointDirection::InOut)) {
        return false;
    }

    bool ok = false;
    if (definition.backend == PointBackend::Modbus &&
        modbus0_ != nullptr &&
        definition.ref.modbus.port_index == 0u &&
        definition.ref.modbus.table == ModbusTable::Coils &&
        (definition.ref.modbus.access == ModbusAccess::Write ||
         definition.ref.modbus.access == ModbusAccess::ReadWrite)) {
        const bool already_applied = current_state.quality == PointQuality::Good && current_state.value.b == value;
        if (already_applied) {
            ok = true;
        } else {
            ok = modbus0_->writeSingleCoil(definition.ref.modbus.slave_address,
                                           definition.ref.modbus.address,
                                           value);
        }
    } else if (definition.backend == PointBackend::Local) {
        ok = true;
    }

    PointCommandState command_state = point_catalog_->commandStates()[catalog_index];
    command_state.last_commanded_value.b = value;
    command_state.last_command_ts_ms = now_ms;
    command_state.pending = false;

    PointState next_state = point_catalog_->states()[catalog_index];
    next_state.value.b = value;
    next_state.last_update_ms = now_ms;

    if (ok) {
        command_state.command_quality = PointCommandQuality::Acked;
        command_state.last_ack_ts_ms = now_ms;
        next_state.quality = PointQuality::Good;
        next_state.last_good_update_ms = now_ms;
    } else {
        command_state.command_quality = PointCommandQuality::ProtocolError;
        next_state.quality = PointQuality::BadWriteRejected;
    }

    const bool state_ok = point_catalog_->updateState(definition.id, next_state);
    const bool command_ok = point_catalog_->updateCommandState(definition.id, command_state);
    return ok && state_ok && command_ok;
}

bool PlcCore::readRuntimeInt16(uint16_t runtime_index, int16_t& value_out) const
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr) {
        return false;
    }

    const size_t catalog_index = runtime_publisher_->catalogIndexForRuntimeIndex(runtime_index);
    if (catalog_index >= point_catalog_->size()) {
        return false;
    }

    const PointDefinition& definition = point_catalog_->entries()[catalog_index];
    const PointState& state = point_catalog_->states()[catalog_index];
    if (definition.value_type != PointValueType::Int16) {
        return false;
    }

    value_out = state.value.i16;
    return true;
}

bool PlcCore::commitRuntimeInt16(uint16_t runtime_index, int16_t value, uint32_t now_ms)
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr) {
        return false;
    }

    const size_t catalog_index = runtime_publisher_->catalogIndexForRuntimeIndex(runtime_index);
    if (catalog_index >= point_catalog_->size()) {
        return false;
    }

    const PointDefinition& definition = point_catalog_->entries()[catalog_index];
    const bool internal_slot_variable = is_dynamic_slot_variable_point(definition);
    if (definition.value_type != PointValueType::Int16 ||
        (!internal_slot_variable &&
         definition.direction != PointDirection::Output &&
         definition.direction != PointDirection::InOut)) {
        return false;
    }

    bool ok = false;
    if (definition.backend == PointBackend::Modbus &&
        modbus0_ != nullptr &&
        definition.ref.modbus.port_index == 0u &&
        definition.ref.modbus.table == ModbusTable::HoldingRegisters &&
        definition.ref.modbus.register_count == 1u &&
        (definition.ref.modbus.access == ModbusAccess::Write ||
         definition.ref.modbus.access == ModbusAccess::ReadWrite)) {
        ok = modbus0_->writeSingleRegister(definition.ref.modbus.slave_address,
                                           definition.ref.modbus.address,
                                           static_cast<uint16_t>(value));
    } else if (definition.backend == PointBackend::Local) {
        ok = true;
    }

    PointCommandState command_state = point_catalog_->commandStates()[catalog_index];
    command_state.last_commanded_value.i16 = value;
    command_state.last_command_ts_ms = now_ms;
    command_state.pending = false;

    PointState next_state = point_catalog_->states()[catalog_index];
    next_state.value.i16 = value;
    next_state.last_update_ms = now_ms;

    if (ok) {
        command_state.command_quality = PointCommandQuality::Acked;
        command_state.last_ack_ts_ms = now_ms;
        next_state.quality = PointQuality::Good;
        next_state.last_good_update_ms = now_ms;
    } else {
        command_state.command_quality = PointCommandQuality::ProtocolError;
        next_state.quality = PointQuality::BadWriteRejected;
    }

    const bool state_ok = point_catalog_->updateState(definition.id, next_state);
    const bool command_ok = point_catalog_->updateCommandState(definition.id, command_state);
    return ok && state_ok && command_ok;
}

void PlcCore::pollNextPoint()
{
    if (point_catalog_ == nullptr || batch_count_ == 0u) {
        return;
    }

    const uint32_t now_ms = millis();
    if (poll_state_ == PollState::WaitingBatchResult) {
        ++active_batch_poll_calls_;
        if (active_batch_last_poll_ms_ != 0u) {
            const uint32_t poll_gap_ms = now_ms - active_batch_last_poll_ms_;
            if (poll_gap_ms > active_batch_max_poll_gap_ms_) {
                active_batch_max_poll_gap_ms_ = poll_gap_ms;
            }
        }
        active_batch_last_poll_ms_ = now_ms;
        switch (modbus0_ == nullptr ? ModbusMaster::TransactionStatus::Error : modbus0_->pollTransaction()) {
            case ModbusMaster::TransactionStatus::Busy:
                return;
            case ModbusMaster::TransactionStatus::Success:
                (void)completeActiveBatch(now_ms);
                poll_state_ = PollState::Idle;
                active_batch_ = {};
                active_batch_started_ms_ = 0u;
                active_batch_last_poll_ms_ = 0u;
                active_batch_max_poll_gap_ms_ = 0u;
                active_batch_poll_calls_ = 0u;
                return;
            case ModbusMaster::TransactionStatus::WatchdogTimeout:
            case ModbusMaster::TransactionStatus::Error:
                failActiveBatch(now_ms,
                                modbus0_ == nullptr ? PointQuality::BadConfigError
                                                    : qualityFromModbusError(modbus0_->lastError()));
                poll_state_ = PollState::Idle;
                active_batch_ = {};
                active_batch_started_ms_ = 0u;
                active_batch_last_poll_ms_ = 0u;
                active_batch_max_poll_gap_ms_ = 0u;
                active_batch_poll_calls_ = 0u;
                return;
            case ModbusMaster::TransactionStatus::Idle:
            default:
                poll_state_ = PollState::Idle;
                active_batch_ = {};
                active_batch_started_ms_ = 0u;
                active_batch_last_poll_ms_ = 0u;
                active_batch_max_poll_gap_ms_ = 0u;
                active_batch_poll_calls_ = 0u;
                return;
        }
    }

    size_t selected_batch_index = batch_count_;
    uint32_t selected_urgency = 0u;
    for (size_t attempts = 0u; attempts < batch_count_; ++attempts) {
        const size_t batch_index = next_batch_index_;
        next_batch_index_ = (next_batch_index_ + 1u) % batch_count_;
        const ModbusPollBatch& batch = batches_[batch_index];
        uint32_t batch_urgency = 0u;
        if (!batch.valid || !batchPollUrgency(batch, now_ms, batch_urgency)) {
            continue;
        }

        if (selected_batch_index == batch_count_ || batch_urgency > selected_urgency) {
            selected_batch_index = batch_index;
            selected_urgency = batch_urgency;
        }
    }

    if (selected_batch_index >= batch_count_) {
        return;
    }

    const ModbusPollBatch& batch = batches_[selected_batch_index];
    next_batch_index_ = (selected_batch_index + 1u) % batch_count_;

    if (startBatchPoll(batch)) {
        active_batch_ = batch;
        active_batch_started_ms_ = now_ms;
        active_batch_last_poll_ms_ = now_ms;
        active_batch_max_poll_gap_ms_ = 0u;
        active_batch_poll_calls_ = 0u;
        poll_state_ = PollState::WaitingBatchResult;
    } else {
        failActiveBatch(now_ms,
                        (batch.port_index != 0u || modbus0_ == nullptr)
                            ? PointQuality::BadConfigError
                            : qualityFromModbusError(modbus0_->lastError()));
    }
}

bool PlcCore::batchPollUrgency(const ModbusPollBatch& batch, uint32_t now_ms, uint32_t& urgency_out) const
{
    urgency_out = 0u;
    if (point_catalog_ == nullptr) {
        return false;
    }

    bool due = false;
    const PointDefinition* definitions = point_catalog_->entries();
    const PointState* states = point_catalog_->states();
    for (uint16_t offset = 0u; offset < batch.member_count; ++offset) {
        const ModbusPollBatchMember& member = batch_members_[batch.member_start + offset];
        const size_t catalog_index = member.catalog_index;
        if (catalog_index >= point_catalog_->size()) {
            continue;
        }

        const PointDefinition& definition = definitions[catalog_index];
        const PointState& state = states[catalog_index];
        if (definition.polling.refresh_ms == 0u) {
            urgency_out = UINT32_MAX;
            return true;
        }

        const uint32_t elapsed_ms = now_ms - state.last_update_ms;
        if (elapsed_ms < definition.polling.refresh_ms) {
            continue;
        }

        due = true;
        const uint32_t overdue_ms = elapsed_ms - definition.polling.refresh_ms;
        if (overdue_ms > urgency_out) {
            urgency_out = overdue_ms;
        }
    }

    return due;
}

bool PlcCore::isBatchDue(const ModbusPollBatch& batch, uint32_t now_ms) const
{
    uint32_t urgency = 0u;
    return batchPollUrgency(batch, now_ms, urgency);
}

bool PlcCore::startBatchPoll(const ModbusPollBatch& batch)
{
    if (point_catalog_ == nullptr || modbus0_ == nullptr || batch.port_index != 0u) {
        return false;
    }

    const bool is_bitwise = modbus_table_is_bitwise(batch.table);
    if (is_bitwise) {
        if (batch.table == ModbusTable::Coils) {
            return modbus0_->startReadCoils(batch.slave_address, batch.start_address, batch.quantity);
        }
        if (batch.table == ModbusTable::DiscreteInputs) {
            return modbus0_->startReadDiscreteInputs(batch.slave_address, batch.start_address, batch.quantity);
        }
        return false;
    }

    if (batch.table == ModbusTable::HoldingRegisters) {
        return modbus0_->startReadHoldingRegisters(batch.slave_address, batch.start_address, batch.quantity);
    }
    if (batch.table == ModbusTable::InputRegisters) {
        return modbus0_->startReadInputRegisters(batch.slave_address, batch.start_address, batch.quantity);
    }

    return false;
}

bool PlcCore::completeActiveBatch(uint32_t now_ms)
{
    if (point_catalog_ == nullptr || modbus0_ == nullptr || !active_batch_.valid) {
        return false;
    }

    const PointDefinition* definitions = point_catalog_->entries();
    const PointState* states = point_catalog_->states();
    const bool is_bitwise = modbus_table_is_bitwise(active_batch_.table);

    bool transaction_ok = false;
    PointQuality batch_error = PointQuality::BadConfigError;
    if (is_bitwise) {
        transaction_ok = modbus0_->finishReadBits(active_bit_values_, active_batch_.quantity);
    } else {
        transaction_ok = modbus0_->finishReadRegisters(active_register_values_, active_batch_.quantity);
    }
    if (!transaction_ok) {
        batch_error = qualityFromModbusError(modbus0_->lastError());
    }

    for (uint16_t offset = 0u; offset < active_batch_.member_count; ++offset) {
        const ModbusPollBatchMember& member = batch_members_[active_batch_.member_start + offset];
        const size_t catalog_index = member.catalog_index;
        if (catalog_index >= point_catalog_->size()) {
            continue;
        }

        const PointDefinition& definition = definitions[catalog_index];
        if (definition.backend != PointBackend::Modbus ||
            definition.ref.modbus.port_index != active_batch_.port_index ||
            definition.ref.modbus.slave_address != active_batch_.slave_address ||
            definition.ref.modbus.table != active_batch_.table ||
            definition.ref.modbus.address < active_batch_.start_address ||
            static_cast<uint16_t>(definition.ref.modbus.address - active_batch_.start_address) != member.address_offset) {
            continue;
        }

        PointState next_state = states[catalog_index];
        bool point_ok = transaction_ok;

        if (transaction_ok) {
            if (is_bitwise) {
                if (member.address_offset >= active_batch_.quantity) {
                    point_ok = false;
                    next_state.quality = PointQuality::BadConfigError;
                } else {
                    point_ok = decodeBitState(definition, active_bit_values_[member.address_offset], next_state);
                }
            } else {
                point_ok = decodeRegisterState(definition,
                                               active_register_values_ + member.address_offset,
                                               static_cast<uint16_t>(active_batch_.quantity - member.address_offset),
                                               next_state);
            }
        } else {
            next_state.quality = batch_error;
        }

        next_state.last_update_ms = now_ms;
        if (point_ok) {
            next_state.last_good_update_ms = now_ms;
        }
        (void)point_catalog_->updateState(definition.id, next_state);
    }

    return transaction_ok;
}

void PlcCore::failActiveBatch(uint32_t now_ms, PointQuality batch_error)
{
    if (point_catalog_ == nullptr || !active_batch_.valid) {
        return;
    }

    const PointDefinition* definitions = point_catalog_->entries();
    const PointState* states = point_catalog_->states();
    for (uint16_t offset = 0u; offset < active_batch_.member_count; ++offset) {
        const ModbusPollBatchMember& member = batch_members_[active_batch_.member_start + offset];
        const size_t catalog_index = member.catalog_index;
        if (catalog_index >= point_catalog_->size()) {
            continue;
        }

        const PointDefinition& definition = definitions[catalog_index];
        if (definition.backend != PointBackend::Modbus ||
            definition.ref.modbus.port_index != active_batch_.port_index ||
            definition.ref.modbus.slave_address != active_batch_.slave_address ||
            definition.ref.modbus.table != active_batch_.table ||
            definition.ref.modbus.address < active_batch_.start_address ||
            static_cast<uint16_t>(definition.ref.modbus.address - active_batch_.start_address) != member.address_offset) {
            continue;
        }

        PointState next_state = states[catalog_index];
        next_state.quality = batch_error;
        next_state.last_update_ms = now_ms;
        (void)point_catalog_->updateState(definition.id, next_state);
    }
}

        bool PlcCore::decodeBitState(const PointDefinition& definition, bool bit_value, PointState& state) const
        {
            (void)definition;
            state.value.b = bit_value;
            state.quality = PointQuality::Good;
            return true;
        }

        bool PlcCore::decodeRegisterState(const PointDefinition& definition,
                                          const uint16_t* regs,
                                          uint16_t available_regs,
                                          PointState& state) const
        {
            if (regs == nullptr || definition.value_type == PointValueType::String || available_regs == 0u) {
                state.quality = PointQuality::BadConfigError;
                return false;
            }

            switch (definition.value_type) {
                case PointValueType::Bool:
                    state.value.b = ModbusMaster::regsToU16(regs[0]) != 0u;
                    break;
                case PointValueType::Uint16:
                    state.value.u16 = static_cast<uint16_t>(apply_numeric_scale(static_cast<float>(ModbusMaster::regsToU16(regs[0])), definition));
                    break;
                case PointValueType::Int16:
                    state.value.i16 = static_cast<int16_t>(apply_numeric_scale(static_cast<float>(ModbusMaster::regsToI16(regs[0])), definition));
                    break;
                case PointValueType::Uint32:
                    if (definition.ref.modbus.register_count < 2u || available_regs < 2u) {
                        state.quality = PointQuality::BadConfigError;
                        return false;
                    }
                    state.value.u32 = static_cast<uint32_t>(apply_numeric_scale(static_cast<float>(ModbusMaster::regsToU32(regs[0], regs[1])), definition));
                    break;
                case PointValueType::Int32:
                    if (definition.ref.modbus.register_count < 2u || available_regs < 2u) {
                        state.quality = PointQuality::BadConfigError;
                        return false;
                    }
                    state.value.i32 = static_cast<int32_t>(apply_numeric_scale(static_cast<float>(ModbusMaster::regsToI32(regs[0], regs[1])), definition));
                    break;
                case PointValueType::Float:
                    if (definition.ref.modbus.register_count >= 2u) {
                        if (available_regs < 2u) {
                            state.quality = PointQuality::BadConfigError;
                            return false;
                        }
                        state.value.f32 = apply_numeric_scale(ModbusMaster::regsToFloatABCD(regs[0], regs[1]), definition);
                        break;
                    }
                    state.value.f32 = apply_numeric_scale(static_cast<float>(ModbusMaster::regsToI16(regs[0])), definition);
                    break;
                case PointValueType::Enum:
                    state.value.enum_value = ModbusMaster::regsToI16(regs[0]);
                    break;
                case PointValueType::String:
                default:
                    state.quality = PointQuality::BadConfigError;
                    return false;
            }

            state.quality = PointQuality::Good;
            return true;
        }

PointQuality PlcCore::qualityFromModbusError(ModbusMaster::Error error) const {
    switch (error) {
        case ModbusMaster::Error::None:
            return PointQuality::Good;
        case ModbusMaster::Error::Busy:
            return PointQuality::UncertainInitialValue;
        case ModbusMaster::Error::InvalidArg:
            return PointQuality::BadConfigError;
        case ModbusMaster::Error::DriverTimeout:
        case ModbusMaster::Error::HwTimeout:
            return PointQuality::BadTimeout;
        case ModbusMaster::Error::HwCrc:
        case ModbusMaster::Error::HwFrame:
        case ModbusMaster::Error::HwException:
        case ModbusMaster::Error::HwOverflow:
        case ModbusMaster::Error::HwUartFrame:
        case ModbusMaster::Error::HwUnknown:
        case ModbusMaster::Error::ResponseMismatch:
        case ModbusMaster::Error::ResponseLength:
        case ModbusMaster::Error::ResponseFormat:
        default:
            return PointQuality::BadProtocolError;
    }
}