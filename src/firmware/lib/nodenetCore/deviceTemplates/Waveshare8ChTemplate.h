#pragma once

inline constexpr DeviceTemplatePoint kWaveshare8ChTemplatePoints[] = {
    {"output1", "Output Channel 1", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 0u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input1", "Input Channel 1", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 0u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output2", "Output Channel 2", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 1u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input2", "Input Channel 2", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 1u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output3", "Output Channel 3", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 2u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input3", "Input Channel 3", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 2u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output4", "Output Channel 4", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 3u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input4", "Input Channel 4", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 3u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output5", "Output Channel 5", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 4u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input5", "Input Channel 5", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 4u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output6", "Output Channel 6", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 5u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input6", "Input Channel 6", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 5u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output7", "Output Channel 7", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 6u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input7", "Input Channel 7", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 6u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
    {"output8", "Output Channel 8", PointDirection::InOut, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 7u, 1u, ModbusTable::Coils, ModbusAccess::ReadWrite},
    {"input8", "Input Channel 8", PointDirection::Input, PointValueType::Bool, 1000u, 3000u, 0u, 1.0f, nullptr, 7u, 1u, ModbusTable::DiscreteInputs, ModbusAccess::Read},
};

inline constexpr DeviceTemplate kWaveshare8ChDeviceTemplate = {
    "waveshare8ch",
    1u,
    kWaveshare8ChTemplatePoints,
    sizeof(kWaveshare8ChTemplatePoints) / sizeof(kWaveshare8ChTemplatePoints[0]),
};