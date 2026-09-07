#include "plclink_mailbox_service.h"

#include <cstring>

#include "PointCatalog.h"
#include "PointState.h"
#include "PlcTypes.h"
#include "protocol_codec.h"
#include "protocol_states.h"
#include "protocol_types.h"
#include "spi_mailbox.h"

namespace plclink_mailbox_service {
namespace {

constexpr uint16_t kMaxMailboxWireResponseBytes = SpiMailbox::kMaxPayloadSize - 3u;

uint32_t encode_state_value_bits(PointValueType value_type, const PointState& state)
{
    switch (value_type) {
    case PointValueType::Bool:
        return state.value.b ? 1u : 0u;
    case PointValueType::Uint16:
        return static_cast<uint32_t>(state.value.u16);
    case PointValueType::Int16:
        return static_cast<uint32_t>(static_cast<uint16_t>(state.value.i16));
    case PointValueType::Uint32:
        return state.value.u32;
    case PointValueType::Int32:
        return static_cast<uint32_t>(state.value.i32);
    case PointValueType::Float: {
        uint32_t bits = 0u;
        std::memcpy(&bits, &state.value.f32, sizeof(bits));
        return bits;
    }
    case PointValueType::Enum:
        return static_cast<uint32_t>(state.value.enum_value);
    case PointValueType::String:
    default:
        return 0u;
    }
}

uint8_t encode_state_flags(PointValueType value_type)
{
    uint8_t flags = plclink::kStateFlagValuePresent;
    if (value_type == PointValueType::String) {
        flags = 0u;
    }
    return flags;
}

bool send_error(SpiMailbox& mailbox,
                uint8_t request_id,
                uint8_t related_message_type,
                plclink::ErrorCode error_code)
{
    uint8_t tx_buffer[SpiMailbox::kMaxPayloadSize] = {};
    plclink::ProtocolHeader header = {};
    header.magic = plclink::kMagic;
    header.version = plclink::kVersion;
    header.message_type = plclink::kMsgError;
    header.flags = static_cast<uint8_t>(plclink::kFlagResponse | plclink::kFlagErrorPayload);
    header.request_id = request_id;
    header.fragment_index = 0u;
    header.fragment_count = 1u;
    header.payload_length = static_cast<uint16_t>(sizeof(plclink::ErrorPayloadV1));

    plclink::ErrorPayloadV1 payload = {};
    payload.error_code = static_cast<uint8_t>(error_code);
    payload.related_message_type = related_message_type;

    if (!plclink::encodeHeader(header, tx_buffer, sizeof(tx_buffer))) {
        return false;
    }
    std::memcpy(&tx_buffer[plclink::kHeaderSize], &payload, sizeof(payload));
    return mailbox.SendMessage(tx_buffer,
                               static_cast<uint16_t>(plclink::kHeaderSize + sizeof(payload)));
}

} // namespace

bool service(SpiMailbox& mailbox,
             const PointCatalog& point_catalog,
             uint32_t defs_generation,
             uint32_t states_sequence)
{
    if (!mailbox.HasMessage() || !mailbox.TxReady()) {
        return false;
    }

    uint8_t rx_buffer[SpiMailbox::kMaxPayloadSize] = {};
    uint16_t request_length = 0u;
    if (!mailbox.ReadMessage(rx_buffer, sizeof(rx_buffer), &request_length)) {
        return false;
    }
    if (request_length < plclink::kHeaderSize) {
        return send_error(mailbox, 0u, 0u, plclink::kErrorMalformedPayload);
    }

    plclink::ProtocolHeader request_header = {};
    if (!plclink::decodeHeader(request_header, rx_buffer, request_length)) {
        return send_error(mailbox, 0u, 0u, plclink::kErrorMalformedPayload);
    }
    if ((request_header.flags & plclink::kFlagRequest) == 0u) {
        return send_error(mailbox,
                          request_header.request_id,
                          request_header.message_type,
                          plclink::kErrorMalformedPayload);
    }

    switch (request_header.message_type) {
    case plclink::kMsgGetCapsReq: {
        if (request_header.payload_length != 0u) {
            return send_error(mailbox,
                              request_header.request_id,
                              request_header.message_type,
                              plclink::kErrorMalformedPayload);
        }

        uint8_t tx_buffer[SpiMailbox::kMaxPayloadSize] = {};
        plclink::ProtocolHeader response_header = {};
        response_header.magic = plclink::kMagic;
        response_header.version = plclink::kVersion;
        response_header.message_type = plclink::kMsgGetCapsRes;
        response_header.flags = static_cast<uint8_t>(plclink::kFlagResponse);
        response_header.request_id = request_header.request_id;
        response_header.fragment_index = 0u;
        response_header.fragment_count = 1u;
        response_header.payload_length = static_cast<uint16_t>(sizeof(plclink::CapsResponsePayload));
        response_header.generation = defs_generation;
        response_header.sequence = states_sequence;

        plclink::CapsResponsePayload payload = {};
        payload.protocol_version = plclink::kVersion;
        payload.capability_flags = plclink::kCapStatesSnapshot;
        payload.max_fragment_payload = static_cast<uint16_t>(kMaxMailboxWireResponseBytes - plclink::kHeaderSize);
        payload.point_count = static_cast<uint16_t>(point_catalog.size());
        payload.defs_generation = defs_generation;
        payload.states_sequence = states_sequence;

        if (!plclink::encodeHeader(response_header, tx_buffer, sizeof(tx_buffer))) {
            return false;
        }
        std::memcpy(&tx_buffer[plclink::kHeaderSize], &payload, sizeof(payload));
        return mailbox.SendMessage(tx_buffer,
                                   static_cast<uint16_t>(plclink::kHeaderSize + sizeof(payload)));
    }
    case plclink::kMsgGetStatesSnapshotReq: {
        if (request_header.payload_length != sizeof(plclink::GetStatesSnapshotRequest) ||
            request_length < (plclink::kHeaderSize + sizeof(plclink::GetStatesSnapshotRequest))) {
            return send_error(mailbox,
                              request_header.request_id,
                              request_header.message_type,
                              plclink::kErrorMalformedPayload);
        }

        plclink::GetStatesSnapshotRequest snapshot_request = {};
        std::memcpy(&snapshot_request,
                    &rx_buffer[plclink::kHeaderSize],
                    sizeof(snapshot_request));

        const uint16_t point_count = static_cast<uint16_t>(point_catalog.size());
        const uint16_t start_index = (snapshot_request.start_index <= point_count)
            ? snapshot_request.start_index
            : point_count;

        constexpr size_t kPrefixSize = sizeof(plclink::StatesSnapshotChunkPrefix);
        constexpr size_t kRecordSize = sizeof(plclink::StateRecordV1);
        const size_t max_payload_bytes = kMaxMailboxWireResponseBytes - plclink::kHeaderSize;
        if (max_payload_bytes < kPrefixSize) {
            return send_error(mailbox,
                              request_header.request_id,
                              request_header.message_type,
                              plclink::kErrorBusyRetryLater);
        }

        const uint16_t transport_record_limit = static_cast<uint16_t>((max_payload_bytes - kPrefixSize) / kRecordSize);
        const uint16_t requested_records = snapshot_request.max_records;
        const uint16_t available_records = static_cast<uint16_t>(point_count - start_index);
        uint16_t returned_records = requested_records;
        if (returned_records > transport_record_limit) {
            returned_records = transport_record_limit;
        }
        if (returned_records > available_records) {
            returned_records = available_records;
        }

        uint8_t tx_buffer[SpiMailbox::kMaxPayloadSize] = {};
        plclink::ProtocolHeader response_header = {};
        response_header.magic = plclink::kMagic;
        response_header.version = plclink::kVersion;
        response_header.message_type = plclink::kMsgStatesSnapshotRes;
        response_header.flags = static_cast<uint8_t>(plclink::kFlagResponse | plclink::kFlagSnapshotPayload);
        response_header.request_id = request_header.request_id;
        response_header.fragment_index = 0u;
        response_header.fragment_count = 1u;
        response_header.generation = defs_generation;
        response_header.sequence = states_sequence;

        plclink::StatesSnapshotChunkPrefix prefix = {};
        prefix.states_sequence_base = states_sequence;
        prefix.total_point_count = point_count;
        prefix.returned_start_index = start_index;
        prefix.returned_record_count = returned_records;
        prefix.more = ((start_index + returned_records) < point_count) ? 1u : 0u;

        std::memcpy(&tx_buffer[plclink::kHeaderSize], &prefix, sizeof(prefix));

        const PointDefinition* definitions = point_catalog.entries();
        const PointState* states = point_catalog.states();
        size_t write_offset = plclink::kHeaderSize + sizeof(prefix);
        for (uint16_t record_index = 0u; record_index < returned_records; ++record_index) {
            const uint16_t point_index = static_cast<uint16_t>(start_index + record_index);
            plclink::StateRecordV1 record = {};
            record.point_index = point_index;
            record.value_type = static_cast<uint8_t>(definitions[point_index].value_type);
            record.state_flags = encode_state_flags(definitions[point_index].value_type);
            record.value_bits = encode_state_value_bits(definitions[point_index].value_type, states[point_index]);
            record.quality = static_cast<uint32_t>(states[point_index].quality);
            record.timestamp_ms = states[point_index].last_update_ms;
            std::memcpy(&tx_buffer[write_offset], &record, sizeof(record));
            write_offset += sizeof(record);
        }

        response_header.payload_length = static_cast<uint16_t>(sizeof(prefix) + (returned_records * sizeof(plclink::StateRecordV1)));
        if (!plclink::encodeHeader(response_header, tx_buffer, sizeof(tx_buffer))) {
            return false;
        }
        return mailbox.SendMessage(tx_buffer,
                                   static_cast<uint16_t>(plclink::kHeaderSize + response_header.payload_length));
    }
    default:
        return send_error(mailbox,
                          request_header.request_id,
                          request_header.message_type,
                          plclink::kErrorUnknownMessageType);
    }
}

} // namespace plclink_mailbox_service