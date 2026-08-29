#pragma once

inline constexpr DeviceTemplatePoint kEurotherm6100TemplatePoints[] = {
    {"ch1", "Eurotherm CH1 PV", PointDirection::Input, PointValueType::Float, 1000u, 3000u, 0u, 0.0001f, "V", 41433u, 1u, ModbusTable::HoldingRegisters, ModbusAccess::Read},
    {"ch2", "Eurotherm CH2 PV", PointDirection::Input, PointValueType::Float, 1000u, 3000u, 0u, 0.0001f, "V", 41436u, 1u, ModbusTable::HoldingRegisters, ModbusAccess::Read},
    {"ch3", "Eurotherm CH3 PV", PointDirection::Input, PointValueType::Float, 1000u, 3000u, 0u, 0.0001f, "V", 41439u, 1u, ModbusTable::HoldingRegisters, ModbusAccess::Read},
};

inline constexpr DeviceTemplate kEurotherm6100DeviceTemplate = {
    "eurotherm6100",
    2u,
    kEurotherm6100TemplatePoints,
    sizeof(kEurotherm6100TemplatePoints) / sizeof(kEurotherm6100TemplatePoints[0]),
};