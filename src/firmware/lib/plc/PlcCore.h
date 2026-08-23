#ifndef PLC_CORE_H
#define PLC_CORE_H

#include <cstddef>
#include <cstdint>

#include "ModbusMaster.h"
#include "PointCatalog.h"

class NodeLogger;

class PlcCore {
public:
    PlcCore() = default;

    void begin(PointCatalog* point_catalog, ModbusMaster* modbus0, NodeLogger* logger = nullptr);
    void loop();

private:
    static constexpr size_t kModbusRegisterBufferSize = 2u;

    PointCatalog* point_catalog_ = nullptr;
    ModbusMaster* modbus0_ = nullptr;
    NodeLogger* logger_ = nullptr;
    size_t next_point_index_ = 0u;

    void pollNextPoint();
    bool pollModbusPoint(const PointDefinition& definition, PointState& state, uint32_t now_ms);
    bool readModbusRegisters(const PointDefinition& definition, uint16_t* regs_out);
    PointQuality qualityFromModbusError(ModbusMaster::Error error) const;
};

#endif