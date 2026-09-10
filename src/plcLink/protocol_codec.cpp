#include "protocol_codec.h"

namespace plclink {
namespace {

void writeLe16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void writeLe32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint16_t readLe16(const uint8_t* in)
{
    return static_cast<uint16_t>(in[0]) |
           (static_cast<uint16_t>(in[1]) << 8);
}

uint32_t readLe32(const uint8_t* in)
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) |
           (static_cast<uint32_t>(in[3]) << 24);
}

} // namespace

bool encodeHeader(const ProtocolHeader& header, uint8_t* out_buffer, size_t out_size)
{
    if ((out_buffer == nullptr) || (out_size < kHeaderSize) || !isHeaderValid(header)) {
        return false;
    }

    writeLe16(&out_buffer[0], header.magic);
    out_buffer[2] = header.version;
    out_buffer[3] = header.message_type;
    out_buffer[4] = header.flags;
    out_buffer[5] = header.request_id;
    out_buffer[6] = header.fragment_index;
    out_buffer[7] = header.fragment_count;
    writeLe16(&out_buffer[8], header.payload_length);
    writeLe32(&out_buffer[10], header.generation);
    writeLe32(&out_buffer[14], header.sequence);
    return true;
}

bool decodeHeader(ProtocolHeader& header, const uint8_t* buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size < kHeaderSize)) {
        return false;
    }

    header.magic = readLe16(&buffer[0]);
    header.version = buffer[2];
    header.message_type = buffer[3];
    header.flags = buffer[4];
    header.request_id = buffer[5];
    header.fragment_index = buffer[6];
    header.fragment_count = buffer[7];
    header.payload_length = readLe16(&buffer[8]);
    header.generation = readLe32(&buffer[10]);
    header.sequence = readLe32(&buffer[14]);
    return isHeaderValid(header);
}

bool isKnownMessageType(uint8_t message_type)
{
    switch (message_type) {
    case kMsgGetCapsReq:
    case kMsgGetCapsRes:
    case kMsgGetDefsSnapshotReq:
    case kMsgDefsSnapshotRes:
    case kMsgGetDefsUpdatesReq:
    case kMsgDefsUpdatesRes:
    case kMsgGetStatesSnapshotReq:
    case kMsgStatesSnapshotRes:
    case kMsgGetStatesUpdatesReq:
    case kMsgStatesUpdatesRes:
    case kMsgWriteDefReq:
    case kMsgWriteDefRes:
    case kMsgWriteStateReq:
    case kMsgWriteStateRes:
    case kMsgAck:
    case kMsgError:
        return true;
    default:
        return false;
    }
}

bool isHeaderFlagsValid(uint8_t flags)
{
    const bool request = (flags & kFlagRequest) != 0u;
    const bool response = (flags & kFlagResponse) != 0u;
    return request != response;
}

bool isHeaderValid(const ProtocolHeader& header)
{
    if (header.magic != kMagic) {
        return false;
    }
    if (header.version != kVersion) {
        return false;
    }
    if (!isKnownMessageType(header.message_type)) {
        return false;
    }
    if (!isHeaderFlagsValid(header.flags)) {
        return false;
    }
    if (header.fragment_count == 0u) {
        return false;
    }
    if (header.fragment_index >= header.fragment_count) {
        return false;
    }
    if ((header.fragment_count == 1u) && (header.fragment_index != 0u)) {
        return false;
    }

    const bool more_fragments = (header.flags & kFlagMoreFragmentsFollow) != 0u;
    const bool is_last = (header.fragment_index + 1u) == header.fragment_count;
    if (more_fragments == is_last) {
        return false;
    }

    return true;
}

} // namespace plclink