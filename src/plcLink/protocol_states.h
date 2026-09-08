#ifndef PLCLINK_PROTOCOL_STATES_H
#define PLCLINK_PROTOCOL_STATES_H

#include <cstddef>
#include <cstdint>

namespace plclink {

static constexpr size_t kStateStringInlineCapacity = 64u;

#pragma pack(push, 1)
struct GetStatesSnapshotRequest {
    uint16_t start_index;
    uint16_t max_records;
    uint8_t include_strings;
    uint8_t reserved0;
    uint16_t reserved1;
};

struct StatesSnapshotChunkPrefix {
    uint32_t states_sequence_base;
    uint16_t total_point_count;
    uint16_t returned_start_index;
    uint16_t returned_record_count;
    uint8_t more;
    uint8_t reserved0;
};

struct StatesUpdatePrefix {
    uint32_t states_sequence;
    uint16_t update_count;
    uint16_t reserved0;
};

struct StateRecordV1 {
    uint16_t point_index;
    uint8_t value_type;
    uint8_t state_flags;
    uint32_t value_bits;
    uint32_t quality;
    uint32_t timestamp_ms;
};

struct StateStringPayloadV1 {
    char string_value[kStateStringInlineCapacity];
};

struct WriteStateRequestV1 {
    uint16_t point_index;
    uint8_t expected_value_type;
    uint8_t write_flags;
    uint32_t value_bits;
};

struct WriteStateResponseV1 {
    uint8_t status_code;
    uint8_t applied_value_type;
    uint16_t reserved0;
    uint32_t result_sequence;
};
#pragma pack(pop)

static_assert(sizeof(StateRecordV1) == 16u, "Unexpected plcLink state record size");
static_assert(sizeof(StateStringPayloadV1) == kStateStringInlineCapacity,
              "Unexpected plcLink state string payload size");

} // namespace plclink

#endif