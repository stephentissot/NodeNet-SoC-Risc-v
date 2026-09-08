#include "plclink_mailbox_service.h"

#include <cstring>

#include "PointCatalog.h"
#include "PointState.h"
#include "PlcTypes.h"
#include "protocol_codec.h"
#include "protocol_defs.h"
#include "protocol_states.h"
#include "protocol_types.h"
#include "spi_mailbox.h"

namespace plclink_mailbox_service {
namespace {

constexpr uint16_t kMaxMailboxWireResponseBytes = SpiMailbox::kMaxPayloadSize - 3u;

template <size_t N>
void copy_text(char (&dst)[N], const char* src)
{
    if (N == 0u) {
        return;
    }

    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }

    std::strncpy(dst, src, N - 1u);
    dst[N - 1u] = '\0';
}

plclink::DefinitionRecordV1 build_definition_record(uint16_t point_index, const PointDefinition& definition)
{
    plclink::DefinitionRecordV1 record = {};
    record.point_index = point_index;
    record.backend = static_cast<uint8_t>(definition.backend);
    record.direction = static_cast<uint8_t>(definition.direction);
    record.value_type = static_cast<uint8_t>(definition.value_type);
    record.string_capacity = definition.string_capacity;
    record.scale = definition.scale;
    copy_text(record.device_id, definition.id.device_id);
    copy_text(record.feature, definition.id.feature);
    copy_text(record.point_id, definition.id.point_id);
    copy_text(record.display_name, definition.display_name);
    copy_text(record.unit, definition.unit);
    return record;
}

size_t state_record_wire_size(const PointDefinition& definition, bool include_strings)
{
    size_t size = sizeof(plclink::StateRecordV1);
    if (include_strings && definition.value_type == PointValueType::String) {
        size += sizeof(plclink::StateStringPayloadV1);
    }
    return size;
}

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

size_t append_state_record(uint8_t* tx_buffer,
                           size_t write_offset,
                           uint16_t point_index,
                           const PointDefinition& definition,
                           const PointState& state,
                           bool include_strings)
{
    plclink::StateRecordV1 record = {};
    record.point_index = point_index;
    record.value_type = static_cast<uint8_t>(definition.value_type);
    record.state_flags = encode_state_flags(definition.value_type);
    record.value_bits = encode_state_value_bits(definition.value_type, state);
    record.quality = static_cast<uint32_t>(state.quality);
    record.timestamp_ms = state.last_update_ms;
    std::memcpy(&tx_buffer[write_offset], &record, sizeof(record));
    write_offset += sizeof(record);

    if (include_strings && definition.value_type == PointValueType::String) {
        plclink::StateStringPayloadV1 string_payload = {};
        copy_text(string_payload.string_value, state.string_value);
        std::memcpy(&tx_buffer[write_offset], &string_payload, sizeof(string_payload));
        write_offset += sizeof(string_payload);
    }

    return write_offset;
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
             PointCatalog& point_catalog,
             ResolvePointStateFn resolve_point_state,
             void* resolve_context,
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
        payload.capability_flags = static_cast<uint16_t>(plclink::kCapDefsSnapshot |
                                 plclink::kCapStatesSnapshot |
                                 plclink::kCapStatesUpdates);
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
    case plclink::kMsgGetDefsSnapshotReq: {
        if (request_header.payload_length != sizeof(plclink::GetDefsSnapshotRequest) ||
            request_length < (plclink::kHeaderSize + sizeof(plclink::GetDefsSnapshotRequest))) {
            return send_error(mailbox,
                              request_header.request_id,
                              request_header.message_type,
                              plclink::kErrorMalformedPayload);
        }

        plclink::GetDefsSnapshotRequest defs_request = {};
        std::memcpy(&defs_request,
                    &rx_buffer[plclink::kHeaderSize],
                    sizeof(defs_request));

        constexpr size_t kPrefixSize = sizeof(plclink::DefsSnapshotChunkPrefix);
        constexpr size_t kRecordSize = sizeof(plclink::DefinitionRecordV1);
        const size_t max_payload_bytes = kMaxMailboxWireResponseBytes - plclink::kHeaderSize;
        if (max_payload_bytes < (kPrefixSize + kRecordSize)) {
            return send_error(mailbox,
                              request_header.request_id,
                              request_header.message_type,
                              plclink::kErrorBusyRetryLater);
        }

        const uint16_t point_count = static_cast<uint16_t>(point_catalog.size());
        const uint32_t total_serialized_bytes = static_cast<uint32_t>(point_count) * static_cast<uint32_t>(kRecordSize);
        const uint32_t clamped_offset = defs_request.resume_offset > total_serialized_bytes
            ? total_serialized_bytes
            : defs_request.resume_offset;
        const uint32_t aligned_offset = (clamped_offset / static_cast<uint32_t>(kRecordSize)) * static_cast<uint32_t>(kRecordSize);
        const uint16_t transport_record_limit = static_cast<uint16_t>((max_payload_bytes - kPrefixSize) / kRecordSize);
        uint16_t requested_record_limit = static_cast<uint16_t>(defs_request.max_chunk_payload / kRecordSize);
        if (requested_record_limit == 0u) {
            requested_record_limit = transport_record_limit;
        }
        if (requested_record_limit > transport_record_limit) {
            requested_record_limit = transport_record_limit;
        }

        const uint16_t start_record = static_cast<uint16_t>(aligned_offset / static_cast<uint32_t>(kRecordSize));
        const uint16_t available_records = start_record < point_count
            ? static_cast<uint16_t>(point_count - start_record)
            : 0u;
        uint16_t returned_records = requested_record_limit;
        if (returned_records > available_records) {
            returned_records = available_records;
        }

        uint8_t tx_buffer[SpiMailbox::kMaxPayloadSize] = {};
        plclink::ProtocolHeader response_header = {};
        response_header.magic = plclink::kMagic;
        response_header.version = plclink::kVersion;
        response_header.message_type = plclink::kMsgDefsSnapshotRes;
        response_header.flags = static_cast<uint8_t>(plclink::kFlagResponse | plclink::kFlagSnapshotPayload);
        response_header.request_id = request_header.request_id;
        response_header.fragment_index = 0u;
        response_header.fragment_count = 1u;
        response_header.generation = defs_generation;
        response_header.sequence = states_sequence;

        plclink::DefsSnapshotChunkPrefix prefix = {};
        prefix.defs_generation = defs_generation;
        prefix.total_serialized_bytes = total_serialized_bytes;
        prefix.total_point_count = point_count;
        prefix.chunk_offset = aligned_offset;
        prefix.chunk_payload_bytes = static_cast<uint16_t>(returned_records * kRecordSize);
        prefix.more = ((start_record + returned_records) < point_count) ? 1u : 0u;

        std::memcpy(&tx_buffer[plclink::kHeaderSize], &prefix, sizeof(prefix));

        const PointDefinition* definitions = point_catalog.entries();
        size_t write_offset = plclink::kHeaderSize + sizeof(prefix);
        for (uint16_t record_index = 0u; record_index < returned_records; ++record_index) {
            const uint16_t point_index = static_cast<uint16_t>(start_record + record_index);
            const plclink::DefinitionRecordV1 record = build_definition_record(point_index, definitions[point_index]);
            std::memcpy(&tx_buffer[write_offset], &record, sizeof(record));
            write_offset += sizeof(record);
        }

        response_header.payload_length = static_cast<uint16_t>(sizeof(prefix) + prefix.chunk_payload_bytes);
        if (!plclink::encodeHeader(response_header, tx_buffer, sizeof(tx_buffer))) {
            return false;
        }
        return mailbox.SendMessage(tx_buffer,
                                   static_cast<uint16_t>(plclink::kHeaderSize + response_header.payload_length));
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
        const bool include_strings = snapshot_request.include_strings != 0u;

        constexpr size_t kPrefixSize = sizeof(plclink::StatesSnapshotChunkPrefix);
        const size_t max_payload_bytes = kMaxMailboxWireResponseBytes - plclink::kHeaderSize;
        if (max_payload_bytes < kPrefixSize) {
            return send_error(mailbox,
                              request_header.request_id,
                              request_header.message_type,
                              plclink::kErrorBusyRetryLater);
        }

        const PointDefinition* definitions = point_catalog.entries();
        const PointState* states = point_catalog.states();

        uint16_t returned_records = 0u;
        size_t payload_budget = max_payload_bytes - kPrefixSize;
        while ((start_index + returned_records) < point_count && returned_records < snapshot_request.max_records) {
            const size_t next_size = state_record_wire_size(definitions[start_index + returned_records], include_strings);
            if (next_size > payload_budget) {
                break;
            }
            payload_budget -= next_size;
            ++returned_records;
        }

        if (((start_index < point_count) && (snapshot_request.max_records != 0u) && (returned_records == 0u)) ||
            ((start_index < point_count) && (snapshot_request.max_records == 0u))) {
            const size_t first_size = state_record_wire_size(definitions[start_index], include_strings);
            if (first_size <= (max_payload_bytes - kPrefixSize)) {
                returned_records = 1u;
            }
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

        size_t write_offset = plclink::kHeaderSize + sizeof(prefix);
        for (uint16_t record_index = 0u; record_index < returned_records; ++record_index) {
            const uint16_t point_index = static_cast<uint16_t>(start_index + record_index);
            PointState virtual_state = {};
            const PointState* response_state = &states[point_index];
            if ((resolve_point_state != nullptr) &&
                resolve_point_state(resolve_context, point_index, definitions[point_index], virtual_state)) {
                response_state = &virtual_state;
            }

            plclink::StateRecordV1 record = {};
            record.point_index = point_index;
            record.value_type = static_cast<uint8_t>(definitions[point_index].value_type);
            record.state_flags = encode_state_flags(definitions[point_index].value_type);
            record.value_bits = encode_state_value_bits(definitions[point_index].value_type, *response_state);
            record.quality = static_cast<uint32_t>(response_state->quality);
            record.timestamp_ms = response_state->last_update_ms;
            std::memcpy(&tx_buffer[write_offset], &record, sizeof(record));
            write_offset += sizeof(record);

            if (include_strings && definitions[point_index].value_type == PointValueType::String) {
                plclink::StateStringPayloadV1 string_payload = {};
                copy_text(string_payload.string_value, response_state->string_value);
                std::memcpy(&tx_buffer[write_offset], &string_payload, sizeof(string_payload));
                write_offset += sizeof(string_payload);
            }
        }

        response_header.payload_length = static_cast<uint16_t>(write_offset - plclink::kHeaderSize);
        if (!plclink::encodeHeader(response_header, tx_buffer, sizeof(tx_buffer))) {
            return false;
        }
        if (prefix.more == 0u) {
            point_catalog.acknowledgeRuntimeFullSync();
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

bool pumpUpdates(SpiMailbox& mailbox,
                 PointCatalog& point_catalog,
                 ResolvePointStateFn resolve_point_state,
                 void* resolve_context,
                 uint32_t defs_generation,
                 uint32_t states_sequence)
{
    if (mailbox.HasMessage() || !mailbox.TxReady()) {
        return false;
    }

    size_t dirty_index = 0u;
    if (!point_catalog.peekDirtyStateIndex(dirty_index)) {
        return false;
    }

    if (dirty_index >= point_catalog.size()) {
        (void)point_catalog.acknowledgeDirtyStateIndex();
        return false;
    }

    const PointDefinition* definitions = point_catalog.entries();
    const PointState* states = point_catalog.states();
    const PointDefinition& definition = definitions[dirty_index];
    PointState virtual_state = {};
    const PointState* response_state = &states[dirty_index];
    if ((resolve_point_state != nullptr) &&
        resolve_point_state(resolve_context,
                            static_cast<uint16_t>(dirty_index),
                            definition,
                            virtual_state)) {
        response_state = &virtual_state;
    }

    uint8_t tx_buffer[SpiMailbox::kMaxPayloadSize] = {};
    plclink::ProtocolHeader response_header = {};
    response_header.magic = plclink::kMagic;
    response_header.version = plclink::kVersion;
    response_header.message_type = plclink::kMsgStatesUpdatesRes;
    response_header.flags = static_cast<uint8_t>(plclink::kFlagResponse | plclink::kFlagUpdatePayload);
    response_header.request_id = 0u;
    response_header.fragment_index = 0u;
    response_header.fragment_count = 1u;
    response_header.generation = defs_generation;
    response_header.sequence = states_sequence;

    plclink::StatesUpdatePrefix prefix = {};
    prefix.states_sequence = states_sequence;
    prefix.update_count = 1u;
    std::memcpy(&tx_buffer[plclink::kHeaderSize], &prefix, sizeof(prefix));

    size_t write_offset = plclink::kHeaderSize + sizeof(prefix);
    write_offset = append_state_record(tx_buffer,
                                       write_offset,
                                       static_cast<uint16_t>(dirty_index),
                                       definition,
                                       *response_state,
                                       true);

    response_header.payload_length = static_cast<uint16_t>(write_offset - plclink::kHeaderSize);
    if (!plclink::encodeHeader(response_header, tx_buffer, sizeof(tx_buffer))) {
        return false;
    }

    if (!mailbox.SendMessage(tx_buffer,
                             static_cast<uint16_t>(plclink::kHeaderSize + response_header.payload_length))) {
        return false;
    }

    return point_catalog.acknowledgeDirtyStateIndex();
}

} // namespace plclink_mailbox_service