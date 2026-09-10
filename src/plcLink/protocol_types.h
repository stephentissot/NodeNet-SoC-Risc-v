#ifndef PLCLINK_PROTOCOL_TYPES_H
#define PLCLINK_PROTOCOL_TYPES_H

#include <cstddef>
#include <cstdint>

namespace plclink {

static constexpr uint16_t kMagic = 0x4C50u;
static constexpr uint8_t kVersion = 1u;
static constexpr size_t kHeaderSize = 18u;

enum HeaderFlags : uint8_t {
    kFlagRequest = 1u << 0,
    kFlagResponse = 1u << 1,
    kFlagAckRequired = 1u << 2,
    kFlagMoreFragmentsFollow = 1u << 3,
    kFlagErrorPayload = 1u << 4,
    kFlagResyncRequired = 1u << 5,
    kFlagSnapshotPayload = 1u << 6,
    kFlagUpdatePayload = 1u << 7,
};

enum MessageType : uint8_t {
    kMsgGetCapsReq = 0x01u,
    kMsgGetCapsRes = 0x02u,
    kMsgGetDefsSnapshotReq = 0x10u,
    kMsgDefsSnapshotRes = 0x11u,
    kMsgGetDefsUpdatesReq = 0x12u,
    kMsgDefsUpdatesRes = 0x13u,
    kMsgGetStatesSnapshotReq = 0x20u,
    kMsgStatesSnapshotRes = 0x21u,
    kMsgGetStatesUpdatesReq = 0x22u,
    kMsgStatesUpdatesRes = 0x23u,
    kMsgWriteDefReq = 0x30u,
    kMsgWriteDefRes = 0x31u,
    kMsgWriteStateReq = 0x32u,
    kMsgWriteStateRes = 0x33u,
    kMsgAck = 0x7Eu,
    kMsgError = 0x7Fu,
};

enum CapabilityFlags : uint16_t {
    kCapDefsSnapshot = 1u << 0,
    kCapDefsUpdates = 1u << 1,
    kCapStatesSnapshot = 1u << 2,
    kCapStatesUpdates = 1u << 3,
    kCapWriteDef = 1u << 4,
    kCapWriteState = 1u << 5,
    kCapStringFetch = 1u << 6,
    kCapTransportAdvertisement = 1u << 7,
};

enum ErrorCode : uint8_t {
    kErrorOk = 0x00u,
    kErrorUnsupportedVersion = 0x01u,
    kErrorUnknownMessageType = 0x02u,
    kErrorMalformedPayload = 0x03u,
    kErrorFragmentOutOfRange = 0x04u,
    kErrorTypeMismatch = 0x05u,
    kErrorPointIndexOutOfRange = 0x06u,
    kErrorWriteRejected = 0x07u,
    kErrorQueueOverflow = 0x08u,
    kErrorResyncRequired = 0x09u,
    kErrorBusyRetryLater = 0x0Au,
};

enum StateFlags : uint8_t {
    kStateFlagValuePresent = 1u << 0,
    kStateFlagStringChanged = 1u << 1,
    kStateFlagCommandRelated = 1u << 2,
    kStateFlagSynthetic = 1u << 3,
    kStateFlagWriteAckSource = 1u << 4,
};

enum WriteStateFlags : uint8_t {
    kWriteStateFlagCommandValue = 1u << 0,
    kWriteStateFlagForceValue = 1u << 1,
    kWriteStateFlagExpectAck = 1u << 2,
};

#pragma pack(push, 1)
struct ProtocolHeader {
    uint16_t magic;
    uint8_t version;
    uint8_t message_type;
    uint8_t flags;
    uint8_t request_id;
    uint8_t fragment_index;
    uint8_t fragment_count;
    uint16_t payload_length;
    uint32_t generation;
    uint32_t sequence;
};

struct CapsResponsePayload {
    uint8_t protocol_version;
    uint8_t reserved0;
    uint16_t capability_flags;
    uint16_t max_fragment_payload;
    uint16_t point_count;
    uint32_t defs_generation;
    uint32_t states_sequence;
};

struct ErrorPayloadV1 {
    uint8_t error_code;
    uint8_t related_message_type;
    uint16_t reserved0;
    uint32_t detail;
};
#pragma pack(pop)

static_assert(sizeof(ProtocolHeader) == kHeaderSize, "Unexpected plcLink header size");

} // namespace plclink

#endif