#ifndef POINT_STATE_H
#define POINT_STATE_H

#include <cstddef>
#include <cstdint>

#include "PlcTypes.h"

static constexpr size_t kPointStringValueCapacity = 64u;

union PointValue {
    bool b;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    float f32;
    int32_t enum_value;
};

struct PointState {
    PointValue value = {};
    char string_value[kPointStringValueCapacity] = {};
    PointQuality quality = PointQuality::Unknown;
    uint32_t last_update_ms = 0u;
    uint32_t last_good_update_ms = 0u;
};

struct PointCommandState {
    PointValue last_commanded_value = {};
    char last_commanded_string[kPointStringValueCapacity] = {};
    PointCommandQuality command_quality = PointCommandQuality::Unknown;
    uint32_t last_command_ts_ms = 0u;
    uint32_t last_ack_ts_ms = 0u;
    bool pending = false;
};

#endif