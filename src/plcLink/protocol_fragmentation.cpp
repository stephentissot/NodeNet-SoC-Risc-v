#include "protocol_fragmentation.h"

namespace plclink {

bool computeFragmentCount(size_t payload_size,
                          size_t max_fragment_payload,
                          uint8_t* out_fragment_count)
{
    if ((out_fragment_count == nullptr) || (max_fragment_payload == 0u)) {
        return false;
    }

    const size_t fragment_count = (payload_size + max_fragment_payload - 1u) / max_fragment_payload;
    if ((fragment_count == 0u) || (fragment_count > 255u)) {
        return false;
    }

    *out_fragment_count = static_cast<uint8_t>(fragment_count);
    return true;
}

bool buildFragmentPlan(size_t payload_size,
                       size_t max_fragment_payload,
                       uint8_t fragment_index,
                       FragmentPlan* out_plan)
{
    if (out_plan == nullptr) {
        return false;
    }

    uint8_t fragment_count = 0u;
    if (!computeFragmentCount(payload_size, max_fragment_payload, &fragment_count)) {
        return false;
    }
    if (fragment_index >= fragment_count) {
        return false;
    }

    const size_t payload_offset = static_cast<size_t>(fragment_index) * max_fragment_payload;
    const size_t remaining = (payload_offset < payload_size) ? (payload_size - payload_offset) : 0u;
    const size_t payload_len = (remaining > max_fragment_payload) ? max_fragment_payload : remaining;
    if (payload_len > 0xFFFFu) {
        return false;
    }

    out_plan->fragment_count = fragment_count;
    out_plan->fragment_index = fragment_index;
    out_plan->fragment_payload_length = static_cast<uint16_t>(payload_len);
    out_plan->payload_offset = payload_offset;
    return true;
}

} // namespace plclink