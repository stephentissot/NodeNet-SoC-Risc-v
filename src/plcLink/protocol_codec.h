#ifndef PLCLINK_PROTOCOL_CODEC_H
#define PLCLINK_PROTOCOL_CODEC_H

#include <cstddef>
#include <cstdint>

#include "protocol_types.h"

namespace plclink {

bool encodeHeader(const ProtocolHeader& header, uint8_t* out_buffer, size_t out_size);
bool decodeHeader(ProtocolHeader& header, const uint8_t* buffer, size_t buffer_size);
bool isKnownMessageType(uint8_t message_type);
bool isHeaderFlagsValid(uint8_t flags);
bool isHeaderValid(const ProtocolHeader& header);

} // namespace plclink

#endif