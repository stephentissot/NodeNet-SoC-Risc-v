#ifndef MODBUS_TYPES_H
#define MODBUS_TYPES_H

#include <cstddef>
#include <cstdint>

enum class ModbusTable : uint8_t {
    Coils = 0x01u,
    DiscreteInputs = 0x02u,
    HoldingRegisters = 0x03u,
    InputRegisters = 0x04u
};

enum class ModbusAccess : uint8_t {
    Read = 0x01u,
    Write = 0x02u,
    ReadWrite = 0x03u
};

enum class ModbusDataType : uint8_t {
    Bool = 0x01u,
    Uint16 = 0x02u,
    Int16 = 0x03u,
    Uint32 = 0x04u,
    Int32 = 0x05u,
    Float = 0x06u
};

struct ModbusComSettings {
    uint32_t baudrate = 9600u;
    uint32_t timeout_ms = 200u;
    uint8_t retries = 1u;
    uint8_t interframe_chars_q1 = 14u;
    uint16_t max_gap = 6u;
};

struct ModbusRegisterDefinition {
    uint8_t slave_address = 1u;
    uint16_t address = 0u;
    char name[32] = {};
    ModbusDataType data_type = ModbusDataType::Uint16;
    ModbusAccess access = ModbusAccess::Read;
    ModbusTable table = ModbusTable::HoldingRegisters;
};

struct ModbusSlaveDefinition {
    uint8_t slave_address = 1u;
    char name[32] = {};
    const ModbusRegisterDefinition* registers = nullptr;
    size_t num_registers = 0u;
};

struct ModbusCatalogSettings {
    ModbusComSettings com = {};
    const ModbusSlaveDefinition* slaves = nullptr;
    size_t num_slaves = 0u;
};

#endif