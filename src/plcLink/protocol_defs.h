#ifndef PLCLINK_PROTOCOL_DEFS_H
#define PLCLINK_PROTOCOL_DEFS_H

#include <cstddef>
#include <cstdint>

namespace plclink {

static constexpr size_t kDefDeviceIdCapacity = 16u;
static constexpr size_t kDefFeatureCapacity = 32u;
static constexpr size_t kDefPointIdCapacity = 32u;
static constexpr size_t kDefDisplayNameCapacity = 32u;
static constexpr size_t kDefUnitCapacity = 10u;

#pragma pack(push, 1)
struct GetDefsSnapshotRequest {
    uint32_t resume_offset;
    uint16_t max_chunk_payload;
    uint16_t reserved0;
    uint32_t requested_generation;
};

struct DefsSnapshotChunkPrefix {
    uint32_t defs_generation;
    uint32_t total_serialized_bytes;
    uint16_t total_point_count;
    uint16_t reserved0;
    uint32_t chunk_offset;
    uint16_t chunk_payload_bytes;
    uint8_t more;
    uint8_t reserved1;
};

struct DefinitionRecordV1 {
    uint16_t point_index;
    uint8_t backend;
    uint8_t direction;
    uint8_t value_type;
    uint8_t reserved0;
    uint16_t string_capacity;
    float scale;
    char device_id[kDefDeviceIdCapacity];
    char feature[kDefFeatureCapacity];
    char point_id[kDefPointIdCapacity];
    char display_name[kDefDisplayNameCapacity];
    char unit[kDefUnitCapacity];
};
#pragma pack(pop)

static_assert(sizeof(DefinitionRecordV1) == 134u, "Unexpected plcLink definition record size");

} // namespace plclink

#endif