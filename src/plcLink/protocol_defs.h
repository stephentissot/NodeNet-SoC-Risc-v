#ifndef PLCLINK_PROTOCOL_DEFS_H
#define PLCLINK_PROTOCOL_DEFS_H

#include <cstdint>

namespace plclink {

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
#pragma pack(pop)

} // namespace plclink

#endif