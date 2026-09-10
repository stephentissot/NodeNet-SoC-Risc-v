#ifndef PLCLINK_PROTOCOL_FRAGMENTATION_H
#define PLCLINK_PROTOCOL_FRAGMENTATION_H

#include <cstddef>
#include <cstdint>

#include "protocol_types.h"

namespace plclink {

struct FragmentPlan {
    uint8_t fragment_count;
    uint8_t fragment_index;
    uint16_t fragment_payload_length;
    size_t payload_offset;
};

bool computeFragmentCount(size_t payload_size,
                          size_t max_fragment_payload,
                          uint8_t* out_fragment_count);
bool buildFragmentPlan(size_t payload_size,
                       size_t max_fragment_payload,
                       uint8_t fragment_index,
                       FragmentPlan* out_plan);

} // namespace plclink

#endif