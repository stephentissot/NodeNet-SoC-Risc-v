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

enum SlotOpcode : uint8_t {
    kSlotOpHalt = 0x00u,
    kSlotOpLoadPointBool = 0x10u,
    kSlotOpStorePointBool = 0x11u,
    kSlotOpIncPointInt16 = 0x20u,
    kSlotOpDecPointInt16 = 0x21u,
};

static constexpr uint32_t kPlcFaultInvalidOpcode = 0x0001u;
static constexpr uint32_t kPlcFaultPointIndexOutOfRange = 0x0004u;
static constexpr uint32_t kPlcFaultTypeMismatch = 0x0005u;
static constexpr uint32_t kPlcFaultWriteRejected = 0x000Du;

static bool plc_slot_paused(const volatile PlcProgramControlBlockV1& control_block)
{
    return (control_block.control & kPlcSlotControlPausedV1) != 0u;
}

static bool modbus_table_is_bitwise(ModbusTable table)
{
    return table == ModbusTable::Coils || table == ModbusTable::DiscreteInputs;
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
    next_vm_scan_ms_ = millis();
    slot0_last_output_valid_ = false;
    slot0_last_output_value_ = false;
    std::memset(batches_, 0, sizeof(batches_));
    std::memset(batch_members_, 0, sizeof(batch_members_));
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

void PlcCore::resetSlot0ExecutionCache()
{
    slot0_last_output_valid_ = false;
    slot0_last_output_value_ = false;
}

void PlcCore::loop() {
    if (point_catalog_ == nullptr) {
        return;
    }

    const uint32_t now_ms = millis();
    consumeRuntimeWrites(now_ms);
    rebuildPollPlanIfNeeded();
    pollNextPoint();
    syncRuntimeSnapshot(now_ms);
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

    volatile PlcPointValueV1* runtime_values = reinterpret_cast<volatile PlcPointValueV1*>(
        static_cast<uintptr_t>(kPlcRuntimeValueBase));
    volatile PlcPointStatusV1* runtime_statuses = reinterpret_cast<volatile PlcPointStatusV1*>(
        static_cast<uintptr_t>(kPlcRuntimeStatusBase));

    const PointDefinition* definitions = point_catalog_->entries();
    for (size_t catalog_index = 0; catalog_index < point_catalog_->size(); ++catalog_index) {
        const uint16_t runtime_index = runtime_publisher_->runtimeIndexForCatalogIndex(catalog_index);
        if (runtime_index == PlcRuntimePublisherV1::kInvalidPointIndex) {
            continue;
        }

        const PointDefinition& definition = definitions[catalog_index];
        volatile const PlcPointStatusV1& runtime_status = runtime_statuses[runtime_index];
        if (runtime_status.last_writer != kPlcRuntimeWriterPlcVm) {
            continue;
        }

        switch (definition.value_type) {
        case PointValueType::Bool: {
            const bool value = (runtime_values[runtime_index].raw0 & 1u) != 0u;
            (void)commitRuntimeBool(runtime_index, value, runtime_status.last_update_ms != 0u
                                                             ? runtime_status.last_update_ms
                                                             : now_ms);
            break;
        }

        case PointValueType::Int16: {
            const int16_t value = static_cast<int16_t>(runtime_values[runtime_index].raw0 & 0xFFFFu);
            (void)commitRuntimeInt16(runtime_index, value, runtime_status.last_update_ms != 0u
                                                               ? runtime_status.last_update_ms
                                                               : now_ms);
            break;
        }

        default:
            break;
        }
    }
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
        if (definitions[index].backend != PointBackend::Modbus) {
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

void PlcCore::runSlot0Program()
{
    if (point_catalog_ == nullptr || runtime_publisher_ == nullptr) {
        return;
    }

    const uint32_t now_ms = millis();
    if ((int32_t)(now_ms - next_vm_scan_ms_) < 0) {
        return;
    }
    next_vm_scan_ms_ = now_ms + kVmScanPeriodMs;

    volatile PlcProgramControlBlockV1* control_block = reinterpret_cast<volatile PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(0u)));
    if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
        control_block->slot_id != 0u ||
        control_block->bytecode_size == 0u ||
        control_block->bytecode_base == 0u ||
        (control_block->status & kPlcSlotStatusFaultedV1) != 0u ||
        plc_slot_paused(*control_block)) {
        return;
    }

    (void)executeSlot0Scan(PlcSlotLoaderV1::slotControlAddress(0u), now_ms);
}

bool PlcCore::executeSlot0Scan(uint32_t control_block_addr, uint32_t now_ms)
{
    volatile PlcProgramControlBlockV1& control_block =
        *reinterpret_cast<volatile PlcProgramControlBlockV1*>(static_cast<uintptr_t>(control_block_addr));

    if (control_block.bytecode_size == 0u) {
        return false;
    }

    const uint8_t* code = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(control_block.bytecode_base));
    const uint32_t entry_pc = control_block.pc < control_block.bytecode_size ? control_block.pc : 0u;
    uint32_t pc = entry_pc;
    uint32_t instructions = 0u;
    bool accumulator = false;

    while (instructions < control_block.max_instructions_per_scan) {
        if (pc >= control_block.bytecode_size) {
            faultSlot0(control_block_addr, kPlcFaultInvalidOpcode, pc);
            return false;
        }

        const uint8_t opcode = code[pc++];
        ++instructions;

        switch (opcode) {
        case kSlotOpHalt:
            control_block.status = kPlcSlotStatusRunningV1;
            control_block.pc = entry_pc;
            control_block.cycle_counter += 1u;
            return true;

        case kSlotOpLoadPointBool: {
            if ((pc + 1u) >= control_block.bytecode_size) {
                faultSlot0(control_block_addr, kPlcFaultPointIndexOutOfRange, pc - 1u);
                return false;
            }

            const uint16_t runtime_index = static_cast<uint16_t>(code[pc]) |
                                           static_cast<uint16_t>(code[pc + 1u] << 8u);
            pc += 2u;
            if (!readRuntimeBool(runtime_index, accumulator)) {
                faultSlot0(control_block_addr, kPlcFaultTypeMismatch, runtime_index);
                return false;
            }
            break;
        }

        case kSlotOpStorePointBool: {
            if ((pc + 1u) >= control_block.bytecode_size) {
                faultSlot0(control_block_addr, kPlcFaultPointIndexOutOfRange, pc - 1u);
                return false;
            }

            const uint16_t runtime_index = static_cast<uint16_t>(code[pc]) |
                                           static_cast<uint16_t>(code[pc + 1u] << 8u);
            pc += 2u;
            if (!slot0_last_output_valid_ || slot0_last_output_value_ != accumulator) {
                if (!commitRuntimeBool(runtime_index, accumulator, now_ms)) {
                    faultSlot0(control_block_addr, kPlcFaultWriteRejected, runtime_index);
                    return false;
                }
                slot0_last_output_valid_ = true;
                slot0_last_output_value_ = accumulator;
            }
            break;
        }

        case kSlotOpIncPointInt16:
        case kSlotOpDecPointInt16: {
            if ((pc + 1u) >= control_block.bytecode_size) {
                faultSlot0(control_block_addr, kPlcFaultPointIndexOutOfRange, pc - 1u);
                return false;
            }

            const uint16_t runtime_index = static_cast<uint16_t>(code[pc]) |
                                           static_cast<uint16_t>(code[pc + 1u] << 8u);
            pc += 2u;

            int16_t current_value = 0;
            if (!readRuntimeInt16(runtime_index, current_value)) {
                faultSlot0(control_block_addr, kPlcFaultTypeMismatch, runtime_index);
                return false;
            }

            const int16_t next_value = static_cast<int16_t>(opcode == kSlotOpIncPointInt16
                                                                 ? current_value + 1
                                                                 : current_value - 1);
            if (!commitRuntimeInt16(runtime_index, next_value, now_ms)) {
                faultSlot0(control_block_addr, kPlcFaultWriteRejected, runtime_index);
                return false;
            }
            break;
        }

        default:
            faultSlot0(control_block_addr, kPlcFaultInvalidOpcode, opcode);
            return false;
        }
    }

    faultSlot0(control_block_addr, 0x000Au, instructions);
    return false;
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
    if (definition.value_type != PointValueType::Bool ||
        (definition.direction != PointDirection::Output && definition.direction != PointDirection::InOut)) {
        return false;
    }

    bool ok = false;
    if (definition.backend == PointBackend::Modbus &&
        modbus0_ != nullptr &&
        definition.ref.modbus.port_index == 0u &&
        definition.ref.modbus.table == ModbusTable::Coils &&
        (definition.ref.modbus.access == ModbusAccess::Write ||
         definition.ref.modbus.access == ModbusAccess::ReadWrite)) {
        ok = modbus0_->writeSingleCoil(definition.ref.modbus.slave_address,
                                       definition.ref.modbus.address,
                                       value);
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
    if (definition.value_type != PointValueType::Int16 ||
        (definition.direction != PointDirection::Output && definition.direction != PointDirection::InOut)) {
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

void PlcCore::faultSlot0(uint32_t control_block_addr, uint32_t fault_code, uint32_t fault_info)
{
    volatile PlcProgramControlBlockV1& control_block = *reinterpret_cast<volatile PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(control_block_addr));
    control_block.status |= kPlcSlotStatusFaultedV1;
    control_block.fault_code = fault_code;
    control_block.fault_info = fault_info;
}

void PlcCore::pollNextPoint()
{
    if (point_catalog_ == nullptr || batch_count_ == 0u) {
        return;
    }

    const uint32_t now_ms = millis();
    for (size_t attempts = 0u; attempts < batch_count_; ++attempts) {
        const size_t batch_index = next_batch_index_;
        next_batch_index_ = (next_batch_index_ + 1u) % batch_count_;
        const ModbusPollBatch& batch = batches_[batch_index];
        if (!batch.valid || !isBatchDue(batch, now_ms)) {
            continue;
        }

        (void)pollBatch(batch, now_ms);
        break;
    }
}

bool PlcCore::isBatchDue(const ModbusPollBatch& batch, uint32_t now_ms) const
{
    if (point_catalog_ == nullptr) {
        return false;
    }

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
        if (definition.polling.refresh_ms == 0u ||
            (now_ms - state.last_update_ms) >= definition.polling.refresh_ms) {
            return true;
        }
    }

    return false;
}

bool PlcCore::pollBatch(const ModbusPollBatch& batch, uint32_t now_ms)
{
    if (point_catalog_ == nullptr) {
        return false;
    }

    const PointDefinition* definitions = point_catalog_->entries();
    const PointState* states = point_catalog_->states();
    const bool is_bitwise = modbus_table_is_bitwise(batch.table);

    bool transaction_ok = false;
    PointQuality batch_error = PointQuality::BadConfigError;
    bool bit_values[kMaxModbusBatchBits] = {};
    uint16_t regs[kMaxModbusBatchRegisters] = {};

    if (batch.port_index != 0u || modbus0_ == nullptr) {
        batch_error = PointQuality::BadConfigError;
    } else if (is_bitwise) {
        transaction_ok = readBatchBits(batch, bit_values);
        if (!transaction_ok) {
            batch_error = qualityFromModbusError(modbus0_->lastError());
        }
    } else {
        transaction_ok = readBatchRegisters(batch, regs);
        if (!transaction_ok) {
            batch_error = qualityFromModbusError(modbus0_->lastError());
        }
    }

    for (uint16_t offset = 0u; offset < batch.member_count; ++offset) {
        const ModbusPollBatchMember& member = batch_members_[batch.member_start + offset];
        const size_t catalog_index = member.catalog_index;
        if (catalog_index >= point_catalog_->size()) {
            continue;
        }

        const PointDefinition& definition = definitions[catalog_index];
        PointState next_state = states[catalog_index];
        bool point_ok = transaction_ok;

        if (transaction_ok) {
            if (is_bitwise) {
                if (member.address_offset >= batch.quantity) {
                    point_ok = false;
                    next_state.quality = PointQuality::BadConfigError;
                } else {
                    point_ok = decodeBitState(definition, bit_values[member.address_offset], next_state);
                }
            } else {
                point_ok = decodeRegisterState(definition,
                                               regs + member.address_offset,
                                               static_cast<uint16_t>(batch.quantity - member.address_offset),
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

        bool PlcCore::readBatchBits(const ModbusPollBatch& batch, bool* bit_values)
        {
            if (bit_values == nullptr || modbus0_ == nullptr) {
                return false;
            }

            if (batch.table == ModbusTable::Coils) {
                return modbus0_->readCoils(batch.slave_address, batch.start_address, batch.quantity, bit_values);
            }
            if (batch.table == ModbusTable::DiscreteInputs) {
                return modbus0_->readDiscreteInputs(batch.slave_address, batch.start_address, batch.quantity, bit_values);
            }

            return false;
        }

        bool PlcCore::readBatchRegisters(const ModbusPollBatch& batch, uint16_t* regs_out)
        {
            if (regs_out == nullptr || modbus0_ == nullptr) {
                return false;
            }

            if (batch.table == ModbusTable::HoldingRegisters) {
                return modbus0_->readHoldingRegisters(batch.slave_address, batch.start_address, batch.quantity, regs_out);
            }
            if (batch.table == ModbusTable::InputRegisters) {
                return modbus0_->readInputRegisters(batch.slave_address, batch.start_address, batch.quantity, regs_out);
            }

            return false;
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