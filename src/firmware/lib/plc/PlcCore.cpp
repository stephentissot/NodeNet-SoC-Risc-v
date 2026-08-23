#include "PlcCore.h"

#include "bigsister.h"
#include "nodenetLogger.h"

namespace {

const char* modbus_error_to_string(ModbusMaster::Error error) {
    switch (error) {
        case ModbusMaster::Error::None:
            return "none";
        case ModbusMaster::Error::Busy:
            return "busy";
        case ModbusMaster::Error::InvalidArg:
            return "invalidArg";
        case ModbusMaster::Error::HwTimeout:
            return "hwTimeout";
        case ModbusMaster::Error::HwCrc:
            return "hwCrc";
        case ModbusMaster::Error::HwFrame:
            return "hwFrame";
        case ModbusMaster::Error::HwException:
            return "hwException";
        case ModbusMaster::Error::HwOverflow:
            return "hwOverflow";
        case ModbusMaster::Error::HwUartFrame:
            return "hwUartFrame";
        case ModbusMaster::Error::HwUnknown:
            return "hwUnknown";
        case ModbusMaster::Error::ResponseMismatch:
            return "responseMismatch";
        case ModbusMaster::Error::ResponseLength:
            return "responseLength";
        case ModbusMaster::Error::ResponseFormat:
            return "responseFormat";
        case ModbusMaster::Error::DriverTimeout:
            return "driverTimeout";
        default:
            return "unknown";
    }
}

}

void PlcCore::begin(PointCatalog* point_catalog, ModbusMaster* modbus0, NodeLogger* logger) {
    point_catalog_ = point_catalog;
    modbus0_ = modbus0;
    logger_ = logger;
    next_point_index_ = 0u;
}

void PlcCore::loop() {
    if (point_catalog_ == nullptr) {
        return;
    }

    pollNextPoint();
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
            if (logger_ != nullptr && stored_state->quality != PointQuality::Good) {
                logger_->Info("Modbus poll recovered %s/%s/%s slave=%u addr=%u table=%u",
                              definition.id.device_id,
                              definition.id.feature,
                              definition.id.point_id,
                              static_cast<unsigned>(definition.ref.modbus.slave_address),
                              static_cast<unsigned>(definition.ref.modbus.address),
                              static_cast<unsigned>(definition.ref.modbus.table));
            }
        } else if (logger_ != nullptr && stored_state->quality != next_state.quality) {
            logger_->Warning("Modbus poll failed %s/%s/%s slave=%u addr=%u table=%u err=%s hw=0x%08lx exc=%u",
                             definition.id.device_id,
                             definition.id.feature,
                             definition.id.point_id,
                             static_cast<unsigned>(definition.ref.modbus.slave_address),
                             static_cast<unsigned>(definition.ref.modbus.address),
                             static_cast<unsigned>(definition.ref.modbus.table),
                             modbus_error_to_string(modbus0_->lastError()),
                             static_cast<unsigned long>(modbus0_->lastHwStatus()),
                             static_cast<unsigned>(modbus0_->lastExceptionCode()));
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
            state.value.u16 = ModbusMaster::regsToU16(regs[0]);
            break;
        case PointValueType::Int16:
            state.value.i16 = ModbusMaster::regsToI16(regs[0]);
            break;
        case PointValueType::Uint32:
            if (definition.ref.modbus.register_count < 2u) {
                state.quality = PointQuality::BadConfigError;
                return false;
            }
            state.value.u32 = ModbusMaster::regsToU32(regs[0], regs[1]);
            break;
        case PointValueType::Int32:
            if (definition.ref.modbus.register_count < 2u) {
                state.quality = PointQuality::BadConfigError;
                return false;
            }
            state.value.i32 = ModbusMaster::regsToI32(regs[0], regs[1]);
            break;
        case PointValueType::Float:
            if (definition.ref.modbus.register_count < 2u) {
                state.quality = PointQuality::BadConfigError;
                return false;
            }
            state.value.f32 = ModbusMaster::regsToFloatABCD(regs[0], regs[1]);
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