#include "PlcCore.h"

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
};

static constexpr uint32_t kPlcFaultInvalidOpcode = 0x0001u;
static constexpr uint32_t kPlcFaultPointIndexOutOfRange = 0x0004u;
static constexpr uint32_t kPlcFaultTypeMismatch = 0x0005u;
static constexpr uint32_t kPlcFaultWriteRejected = 0x000Du;

static bool plc_slot_paused(const volatile PlcProgramControlBlockV1& control_block)
{
    return (control_block.control & kPlcSlotControlPausedV1) != 0u;
}

}

void PlcCore::begin(PointCatalog* point_catalog, ModbusMaster* modbus0, NodeLogger* logger) {
    point_catalog_ = point_catalog;
    modbus0_ = modbus0;
    logger_ = logger;
    next_point_index_ = 0u;
    next_vm_scan_ms_ = millis();
    slot0_last_output_valid_ = false;
    slot0_last_output_value_ = false;
}

void PlcCore::attachRuntimePublisher(const PlcRuntimePublisherV1* publisher)
{
    runtime_publisher_ = publisher;
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

    pollNextPoint();
    runSlot0Program();
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

void PlcCore::faultSlot0(uint32_t control_block_addr, uint32_t fault_code, uint32_t fault_info)
{
    volatile PlcProgramControlBlockV1& control_block =
        *reinterpret_cast<volatile PlcProgramControlBlockV1*>(static_cast<uintptr_t>(control_block_addr));
    control_block.status = kPlcSlotStatusFaultedV1;
    control_block.fault_code = fault_code;
    control_block.fault_info = fault_info;
}

void PlcCore::pollNextPoint() {
    const size_t point_count = point_catalog_->size();
    if (point_count == 0u) {
        return;
    }

    const uint32_t now_ms = millis();
    const PointDefinition* definitions = point_catalog_->entries();
    for (size_t attempts = 0u; attempts < point_count; ++attempts) {
        const size_t index = next_point_index_;
        next_point_index_ = (next_point_index_ + 1u) % point_count;

        const PointDefinition& definition = definitions[index];
        if (definition.backend != PointBackend::Modbus) {
            continue;
        }

        PointState* stored_state = point_catalog_->findState(definition.id);
        if (stored_state == nullptr) {
            continue;
        }

        if (definition.polling.refresh_ms != 0u &&
            (now_ms - stored_state->last_update_ms) < definition.polling.refresh_ms) {
            continue;
        }

        PointState next_state = *stored_state;
        const bool ok = pollModbusPoint(definition, next_state, now_ms);
        if (ok) {
            next_state.last_good_update_ms = now_ms;
        }
        next_state.last_update_ms = now_ms;
        (void)point_catalog_->updateState(definition.id, next_state);
        break;
    }
}

bool PlcCore::pollModbusPoint(const PointDefinition& definition, PointState& state, uint32_t now_ms) {
    (void)now_ms;

    if (definition.ref.modbus.port_index != 0u || modbus0_ == nullptr) {
        state.quality = PointQuality::BadConfigError;
        return false;
    }

    if (definition.value_type == PointValueType::String || definition.ref.modbus.register_count == 0u ||
        definition.ref.modbus.register_count > kModbusRegisterBufferSize) {
        state.quality = PointQuality::BadConfigError;
        return false;
    }

    if (definition.ref.modbus.table == ModbusTable::Coils ||
        definition.ref.modbus.table == ModbusTable::DiscreteInputs) {
        bool bit_value = false;
        bool ok = false;
        if (definition.ref.modbus.table == ModbusTable::Coils) {
            ok = modbus0_->readCoils(definition.ref.modbus.slave_address,
                                     definition.ref.modbus.address,
                                     1u,
                                     &bit_value);
        } else {
            ok = modbus0_->readDiscreteInputs(definition.ref.modbus.slave_address,
                                              definition.ref.modbus.address,
                                              1u,
                                              &bit_value);
        }

        if (!ok) {
            state.quality = qualityFromModbusError(modbus0_->lastError());
            return false;
        }

        state.value.b = bit_value;
        state.quality = PointQuality::Good;
        return true;
    }

    uint16_t regs[kModbusRegisterBufferSize] = {};
    if (!readModbusRegisters(definition, regs)) {
        state.quality = qualityFromModbusError(modbus0_->lastError());
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
            if (definition.ref.modbus.register_count < 2u) {
                state.quality = PointQuality::BadConfigError;
                return false;
            }
            state.value.u32 = static_cast<uint32_t>(apply_numeric_scale(static_cast<float>(ModbusMaster::regsToU32(regs[0], regs[1])), definition));
            break;
        case PointValueType::Int32:
            if (definition.ref.modbus.register_count < 2u) {
                state.quality = PointQuality::BadConfigError;
                return false;
            }
            state.value.i32 = static_cast<int32_t>(apply_numeric_scale(static_cast<float>(ModbusMaster::regsToI32(regs[0], regs[1])), definition));
            break;
        case PointValueType::Float:
            if (definition.ref.modbus.register_count >= 2u) {
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

bool PlcCore::readModbusRegisters(const PointDefinition& definition, uint16_t* regs_out) {
    if (regs_out == nullptr) {
        return false;
    }

    if (definition.ref.modbus.table == ModbusTable::HoldingRegisters) {
        return modbus0_->readHoldingRegisters(definition.ref.modbus.slave_address,
                                              definition.ref.modbus.address,
                                              definition.ref.modbus.register_count,
                                              regs_out);
    }
    if (definition.ref.modbus.table == ModbusTable::InputRegisters) {
        return modbus0_->readInputRegisters(definition.ref.modbus.slave_address,
                                            definition.ref.modbus.address,
                                            definition.ref.modbus.register_count,
                                            regs_out);
    }

    return false;
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