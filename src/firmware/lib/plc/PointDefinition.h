#ifndef POINT_DEFINITION_H
#define POINT_DEFINITION_H

#include <cstddef>
#include <cstdint>

#include "PlcTypes.h"
#include "ModbusTypes.h"

struct PollingSettings {
    uint32_t refresh_ms = 1000u;
    uint32_t timeout_ms = 3000u;
};

struct PointIdentity {
    char device_id[16] = {};
    char feature[32] = {};
    char point_id[32] = {};
};

struct EnumEntry {
    int32_t value = 0;
    char label[24] = {};
};

struct EnumDefinition {
    const EnumEntry* entries = nullptr;
    size_t count = 0u;
};

struct ModbusPointRef {
    uint8_t port_index = 0u;
    uint8_t slave_address = 1u;
    uint16_t address = 0u;
    uint8_t register_count = 1u;
    ModbusTable table = ModbusTable::HoldingRegisters;
    ModbusAccess access = ModbusAccess::Read;
};

struct NodeNetPointRef {
    char remote_device_id[16] = {};
    char remote_feature[32] = {};
    char remote_point_id[32] = {};
};

union PointBackendRef {
    ModbusPointRef modbus;
    NodeNetPointRef nodenet;
};

struct PointDefinition {
    PointIdentity id = {};
    char display_name[32] = {};
    PointBackend backend = PointBackend::Local;
    PointDirection direction = PointDirection::Input;
    PointValueType value_type = PointValueType::Bool;
    PollingSettings polling = {};
    uint16_t string_capacity = 0u;
    const EnumDefinition* enum_def = nullptr;
    PointBackendRef ref = {};
};

#endif