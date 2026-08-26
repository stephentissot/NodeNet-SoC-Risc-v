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
    void loop();

private:
    static constexpr size_t kModbusRegisterBufferSize = 2u;
    static constexpr uint32_t kVmScanPeriodMs = 50u;

    PointCatalog* point_catalog_ = nullptr;
    ModbusMaster* modbus0_ = nullptr;
    NodeLogger* logger_ = nullptr;
    const PlcRuntimePublisherV1* runtime_publisher_ = nullptr;
    size_t next_point_index_ = 0u;
    uint32_t next_vm_scan_ms_ = 0u;
    bool slot0_last_output_valid_ = false;
    bool slot0_last_output_value_ = false;

    void pollNextPoint();
    void runSlot0Program();
    bool executeSlot0Scan(uint32_t control_block_addr, uint32_t now_ms);
    bool readRuntimeBool(uint16_t runtime_index, bool& value_out) const;
    bool commitRuntimeBool(uint16_t runtime_index, bool value, uint32_t now_ms);
    void faultSlot0(uint32_t control_block_addr, uint32_t fault_code, uint32_t fault_info);
    bool pollModbusPoint(const PointDefinition& definition, PointState& state, uint32_t now_ms);
    bool readModbusRegisters(const PointDefinition& definition, uint16_t* regs_out);
    PointQuality qualityFromModbusError(ModbusMaster::Error error) const;
};

#endif