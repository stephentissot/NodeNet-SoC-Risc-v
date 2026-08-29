#pragma once

#include <cstddef>
#include <cstdint>

#include "PointDefinition.h"

struct DeviceTemplatePoint {
    const char* point_id;
    const char* display_name;
    PointDirection direction;
    PointValueType value_type;
    uint32_t refresh_ms;
    uint32_t timeout_ms;
    uint16_t string_capacity;
    float scale;
    const char* unit;
    uint16_t address;
    uint8_t register_count;
    ModbusTable table;
    ModbusAccess access;
};

struct DeviceTemplate {
    const char* template_id;
    uint8_t default_slave_address;
    const DeviceTemplatePoint* points;
    size_t point_count;
};

#include "Waveshare8ChTemplate.h"
#include "Eurotherm6100Template.h"

inline const DeviceTemplate* find_device_template_by_id(const char* template_id)
{
    if (template_id == nullptr) {
        return nullptr;
    }

    static constexpr const DeviceTemplate* kTemplates[] = {
        &kWaveshare8ChDeviceTemplate,
        &kEurotherm6100DeviceTemplate,
    };

    for (const DeviceTemplate* device_template : kTemplates) {
        if (device_template == nullptr || device_template->template_id == nullptr) {
            continue;
        }
        if (std::strcmp(device_template->template_id, template_id) == 0) {
            return device_template;
        }
    }

    return nullptr;
}