#include "spi_link.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <esp_check.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>

#include "app_config.h"
#include "protocol_codec.h"
#include "protocol_defs.h"
#include "protocol_states.h"
#include "protocol_types.h"
#include "spi_bus_shared.h"

namespace spi_link {
namespace {

constexpr const char* kLogTag = "spi-link";
constexpr bool kEnableMailboxTraceLogs = false;
constexpr uint8_t kPointValueTypeBool = 0u;
constexpr uint8_t kPointValueTypeString = 7u;
constexpr uint8_t kPointValueTypeUint32 = 3u;
constexpr uint8_t kOpcodeReadStatus = 0x01;
constexpr uint8_t kOpcodeWriteRequest = 0x02;
constexpr uint8_t kOpcodeReadResponse = 0x03;
constexpr uint8_t kOpcodeWriteControl = 0x04;
constexpr uint16_t kControlClearIrq = 1u << 2;
constexpr uint16_t kControlResetMailbox = 1u << 3;
constexpr size_t kMaxMailboxPayload = 256;
constexpr uint8_t kBootPercentLinkReady = 25u;
constexpr uint8_t kBootPercentCapsReady = 45u;
constexpr uint8_t kBootPercentSnapshotBase = 45u;
constexpr uint8_t kBootPercentSnapshotSpan = 54u;
constexpr size_t kMaxPendingPointUpdates = 512u;
constexpr TickType_t kStateReadLockTimeoutTicks = 1u;

struct PendingPointUpdate {
    uint16_t point_index;
    uint32_t sequence;
};

struct PendingWriteStateRequest {
    bool valid;
    uint16_t point_index;
    uint8_t expected_value_type;
    uint8_t write_flags;
    uint32_t value_bits;
};

DMA_ATTR uint8_t g_mailbox_tx_buffer[3 + kMaxMailboxPayload] = {};
DMA_ATTR uint8_t g_mailbox_rx_buffer[3 + kMaxMailboxPayload] = {};
spi_device_handle_t g_fpga_device = nullptr;
int g_last_irq_level = -1;
uint32_t g_poll_count = 0;
uint32_t g_frame_count = 0;
uint16_t g_last_status = 0xFFFFu;
uint32_t g_runtime_status5_ready_count = 0u;
uint32_t g_runtime_suspicious_mailbox_count = 0u;
bool g_startup_irq_inconsistent_logged = false;
bool g_caps_request_sent = false;
bool g_caps_response_seen = false;
bool g_defs_request_sent = false;
bool g_defs_response_seen = false;
bool g_states_request_sent = false;
bool g_states_response_seen = false;
bool g_resync_pending = false;
uint32_t g_defs_generation = 0u;
uint32_t g_defs_total_bytes = 0u;
uint32_t g_defs_loaded_bytes = 0u;
uint32_t g_next_defs_offset = 0u;
uint16_t g_next_states_start_index = 0u;
uint8_t g_next_request_id = 1u;
uint16_t g_point_count = 0u;
uint16_t g_max_fragment_payload = 0u;
uint16_t g_snapshot_chunk_count = 0u;
uint16_t g_snapshot_record_count = 0u;
uint32_t g_states_sequence = 0u;
bool g_refresh_requested = false;
bool g_write_state_request_sent = false;
PendingWriteStateRequest g_pending_write_state = {};
SemaphoreHandle_t g_state_mutex = nullptr;
std::vector<plclink::DefinitionRecordV1> g_definition_records;
std::vector<CachedStateRecord> g_state_records;
PendingPointUpdate g_pending_point_updates[kMaxPendingPointUpdates] = {};
size_t g_pending_point_update_head = 0u;
size_t g_pending_point_update_count = 0u;

esp_err_t write_control(uint16_t control_word);
esp_err_t finalize_response_read(const char* context);
esp_err_t read_response(uint8_t* out_payload, uint16_t capacity, uint16_t* out_payload_len);
esp_err_t write_request(const uint8_t* payload, uint16_t payload_len);
uint8_t next_request_id();

void state_lock()
{
    if (g_state_mutex != nullptr) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    }
}

bool state_try_lock(TickType_t timeout_ticks)
{
    if (g_state_mutex == nullptr) {
        return true;
    }

    return xSemaphoreTake(g_state_mutex, timeout_ticks) == pdTRUE;
}

void state_unlock()
{
    if (g_state_mutex != nullptr) {
        xSemaphoreGive(g_state_mutex);
    }
}

void clear_pending_point_updates_locked()
{
    g_pending_point_update_head = 0u;
    g_pending_point_update_count = 0u;
}

void enqueue_pending_point_update_locked(uint16_t point_index, uint32_t sequence)
{
    if (g_pending_point_update_count == kMaxPendingPointUpdates) {
        g_pending_point_update_head = (g_pending_point_update_head + 1u) % kMaxPendingPointUpdates;
        --g_pending_point_update_count;
    }

    const size_t tail = (g_pending_point_update_head + g_pending_point_update_count) % kMaxPendingPointUpdates;
    g_pending_point_updates[tail].point_index = point_index;
    g_pending_point_updates[tail].sequence = sequence;
    ++g_pending_point_update_count;
}

void reset_state_cache(uint16_t point_count)
{
    state_lock();
    g_state_records.clear();
    g_state_records.resize(point_count);
    g_states_sequence = 0u;
    clear_pending_point_updates_locked();
    state_unlock();
}

void reset_defs_cache()
{
    state_lock();
    g_definition_records.clear();
    g_defs_total_bytes = 0u;
    g_defs_loaded_bytes = 0u;
    state_unlock();
}

void begin_full_resync(uint32_t new_defs_generation)
{
    g_resync_pending = true;
    g_caps_request_sent = false;
    g_caps_response_seen = false;
    g_defs_request_sent = false;
    g_defs_response_seen = false;
    g_states_request_sent = false;
    g_states_response_seen = false;
    g_refresh_requested = false;
    g_point_count = 0u;
    g_max_fragment_payload = 0u;
    g_defs_generation = new_defs_generation;
    g_states_sequence = 0u;
    g_defs_total_bytes = 0u;
    g_defs_loaded_bytes = 0u;
    g_next_defs_offset = 0u;
    g_next_states_start_index = 0u;
    g_snapshot_chunk_count = 0u;
    g_snapshot_record_count = 0u;
    g_write_state_request_sent = false;
    g_pending_write_state = {};
    reset_defs_cache();
    reset_state_cache(0u);
}

esp_err_t send_write_state_request(const PendingWriteStateRequest& request)
{
    uint8_t request_buffer[plclink::kHeaderSize + sizeof(plclink::WriteStateRequestV1)] = {};
    plclink::ProtocolHeader header = {};
    header.magic = plclink::kMagic;
    header.version = plclink::kVersion;
    header.message_type = plclink::kMsgWriteStateReq;
    header.flags = static_cast<uint8_t>(plclink::kFlagRequest);
    header.request_id = next_request_id();
    header.fragment_index = 0u;
    header.fragment_count = 1u;
    header.payload_length = static_cast<uint16_t>(sizeof(plclink::WriteStateRequestV1));
    if (!plclink::encodeHeader(header, request_buffer, sizeof(request_buffer))) {
        return ESP_ERR_INVALID_ARG;
    }

    plclink::WriteStateRequestV1 payload = {};
    payload.point_index = request.point_index;
    payload.expected_value_type = request.expected_value_type;
    payload.write_flags = request.write_flags;
    payload.value_bits = request.value_bits;
    std::memcpy(&request_buffer[plclink::kHeaderSize], &payload, sizeof(payload));

    ESP_RETURN_ON_ERROR(write_request(request_buffer, sizeof(request_buffer)),
                        kLogTag,
                        "write state request failed");
    g_write_state_request_sent = true;
    ESP_LOGI(kLogTag,
             "TX plcLink write_state request point=%u type=%u flags=0x%02x",
             static_cast<unsigned>(payload.point_index),
             static_cast<unsigned>(payload.expected_value_type),
             static_cast<unsigned>(payload.write_flags));
    return ESP_OK;
}

uint16_t compute_max_defs_payload_bytes()
{
    if (g_max_fragment_payload <= sizeof(plclink::DefsSnapshotChunkPrefix)) {
        return 0u;
    }

    return static_cast<uint16_t>(g_max_fragment_payload - sizeof(plclink::DefsSnapshotChunkPrefix));
}

void store_defs_chunk(const plclink::DefsSnapshotChunkPrefix& prefix, const uint8_t* chunk_bytes)
{
    state_lock();
    if ((g_definition_records.size() != prefix.total_point_count) ||
        (g_defs_total_bytes != prefix.total_serialized_bytes)) {
        g_definition_records.assign(prefix.total_point_count, {});
        g_defs_total_bytes = prefix.total_serialized_bytes;
        g_defs_loaded_bytes = 0u;
    }

    const size_t record_size = sizeof(plclink::DefinitionRecordV1);
    if ((prefix.chunk_offset % record_size) == 0u &&
        (prefix.chunk_payload_bytes % record_size) == 0u) {
        const size_t first_record = prefix.chunk_offset / record_size;
        const size_t record_count = prefix.chunk_payload_bytes / record_size;
        if ((first_record + record_count) <= g_definition_records.size()) {
            std::memcpy(g_definition_records.data() + first_record,
                        chunk_bytes,
                        record_count * record_size);
            const uint32_t next_loaded = prefix.chunk_offset + prefix.chunk_payload_bytes;
            if (next_loaded > g_defs_loaded_bytes) {
                g_defs_loaded_bytes = next_loaded;
            }
        }
    }
    state_unlock();
}

bool state_record_has_inline_string(const plclink::StateRecordV1& record)
{
    return record.value_type == kPointValueTypeString;
}

template <size_t N>
bool record_text_equals(const char (&record_text)[N], const char* value)
{
    if (value == nullptr) {
        return false;
    }

    const size_t record_len = strnlen(record_text, N);
    const size_t value_len = std::strlen(value);
    return (record_len == value_len) && (std::memcmp(record_text, value, record_len) == 0);
}

bool find_state_record_by_path_locked(const char* feature,
                                      const char* point_id,
                                      uint8_t expected_value_type,
                                      const CachedStateRecord** out_state_record)
{
    if ((feature == nullptr) || (point_id == nullptr) || (out_state_record == nullptr)) {
        return false;
    }

    const size_t count = g_definition_records.size();
    for (size_t index = 0u; index < count; ++index) {
        const auto& definition = g_definition_records[index];
        if ((definition.value_type != expected_value_type) ||
            !record_text_equals(definition.feature, feature) ||
            !record_text_equals(definition.point_id, point_id)) {
            continue;
        }

        const size_t state_index = static_cast<size_t>(definition.point_index);
        if (state_index >= g_state_records.size()) {
            continue;
        }

        const auto& state_record = g_state_records[state_index];
        if ((state_record.record.point_index != definition.point_index) ||
            (state_record.record.value_type != expected_value_type)) {
            continue;
        }

        *out_state_record = &state_record;
        return true;
    }

    return false;
}

void store_snapshot_chunk(const plclink::StatesSnapshotChunkPrefix& prefix, const uint8_t* record_bytes, size_t payload_bytes)
{
    state_lock();
    if (g_state_records.size() < prefix.total_point_count) {
        g_state_records.resize(prefix.total_point_count);
    }

    size_t read_offset = 0u;
    for (uint16_t index = 0u; index < prefix.returned_record_count; ++index) {
        if ((read_offset + sizeof(plclink::StateRecordV1)) > payload_bytes) {
            break;
        }

        CachedStateRecord cached = {};
        std::memcpy(&cached.record,
                    record_bytes + read_offset,
                    sizeof(cached.record));
        read_offset += sizeof(cached.record);

        if (state_record_has_inline_string(cached.record)) {
            if ((read_offset + sizeof(plclink::StateStringPayloadV1)) > payload_bytes) {
                break;
            }

            plclink::StateStringPayloadV1 string_payload = {};
            std::memcpy(&string_payload,
                        record_bytes + read_offset,
                        sizeof(string_payload));
            std::memcpy(cached.string_value,
                        string_payload.string_value,
                        sizeof(string_payload.string_value));
            read_offset += sizeof(string_payload);
        }

        const size_t target_index = static_cast<size_t>(cached.record.point_index);
        if (target_index < g_state_records.size()) {
            g_state_records[target_index] = cached;
            enqueue_pending_point_update_locked(cached.record.point_index, prefix.states_sequence_base);
        }
    }

    g_states_sequence = prefix.states_sequence_base;
    state_unlock();
}

bool measure_state_records_payload(const uint8_t* chunk_bytes,
                                   size_t chunk_payload_bytes,
                                   uint16_t record_count,
                                   size_t* out_payload_bytes)
{
    if ((chunk_bytes == nullptr) || (out_payload_bytes == nullptr)) {
        return false;
    }

    size_t expected_records_payload = 0u;
    size_t scan_offset = 0u;
    for (uint16_t index = 0u; index < record_count; ++index) {
        if ((scan_offset + sizeof(plclink::StateRecordV1)) > chunk_payload_bytes) {
            return false;
        }

        plclink::StateRecordV1 record = {};
        std::memcpy(&record, chunk_bytes + scan_offset, sizeof(record));
        scan_offset += sizeof(record);
        expected_records_payload += sizeof(record);
        if (state_record_has_inline_string(record)) {
            if ((scan_offset + sizeof(plclink::StateStringPayloadV1)) > chunk_payload_bytes) {
                return false;
            }
            scan_offset += sizeof(plclink::StateStringPayloadV1);
            expected_records_payload += sizeof(plclink::StateStringPayloadV1);
        }
    }

    *out_payload_bytes = expected_records_payload;
    return true;
}

void store_update_chunk(const plclink::StatesUpdatePrefix& prefix,
                        const uint8_t* record_bytes,
                        size_t payload_bytes)
{
    state_lock();

    size_t read_offset = 0u;
    for (uint16_t index = 0u; index < prefix.update_count; ++index) {
        if ((read_offset + sizeof(plclink::StateRecordV1)) > payload_bytes) {
            break;
        }

        CachedStateRecord cached = {};
        std::memcpy(&cached.record,
                    record_bytes + read_offset,
                    sizeof(cached.record));
        read_offset += sizeof(cached.record);

        if (state_record_has_inline_string(cached.record)) {
            if ((read_offset + sizeof(plclink::StateStringPayloadV1)) > payload_bytes) {
                break;
            }

            plclink::StateStringPayloadV1 string_payload = {};
            std::memcpy(&string_payload,
                        record_bytes + read_offset,
                        sizeof(string_payload));
            std::memcpy(cached.string_value,
                        string_payload.string_value,
                        sizeof(string_payload.string_value));
            read_offset += sizeof(string_payload);
        }

        const size_t target_index = static_cast<size_t>(cached.record.point_index);
        if (target_index >= g_state_records.size()) {
            g_state_records.resize(target_index + 1u);
        }
        g_state_records[target_index] = cached;
        enqueue_pending_point_update_locked(cached.record.point_index, prefix.states_sequence);
    }

    g_states_sequence = prefix.states_sequence;
    state_unlock();
}

esp_err_t process_state_update_frame(const uint8_t* payload,
                                     uint16_t payload_len,
                                     const char* context)
{
    if (payload_len < (plclink::kHeaderSize + sizeof(plclink::StatesUpdatePrefix))) {
        ESP_LOGW(kLogTag,
                 "Ignored short plcLink state update%s len=%u",
                 context,
                 static_cast<unsigned>(payload_len));
        return ESP_OK;
    }

    plclink::ProtocolHeader header = {};
    if (!plclink::decodeHeader(header, payload, payload_len)) {
        ESP_LOGW(kLogTag, "Ignored invalid plcLink state update%s", context);
        return ESP_OK;
    }

    plclink::StatesUpdatePrefix prefix = {};
    std::memcpy(&prefix, &payload[plclink::kHeaderSize], sizeof(prefix));
    const uint8_t* chunk_bytes = &payload[plclink::kHeaderSize + sizeof(prefix)];
    const size_t chunk_payload_bytes = header.payload_length - sizeof(prefix);
    size_t expected_records_payload = 0u;
    if (!measure_state_records_payload(chunk_bytes,
                                       chunk_payload_bytes,
                                       prefix.update_count,
                                       &expected_records_payload)) {
        ESP_LOGW(kLogTag,
                 "Ignored malformed plcLink state update%s",
                 context);
        return ESP_OK;
    }

    const size_t expected_payload = sizeof(prefix) + expected_records_payload;
    if (header.payload_length < expected_payload || payload_len < (plclink::kHeaderSize + expected_payload)) {
        ESP_LOGW(kLogTag,
                 "Ignored truncated plcLink state update%s",
                 context);
        return ESP_OK;
    }

    ++g_frame_count;
    store_update_chunk(prefix, chunk_bytes, expected_records_payload);
    plclink::StateRecordV1 first_record = {};
    if (prefix.update_count != 0u && expected_records_payload >= sizeof(first_record)) {
        std::memcpy(&first_record, chunk_bytes, sizeof(first_record));
    }
    if (kEnableMailboxTraceLogs) {
        ESP_LOGI(kLogTag,
                 "RX plcLink state update #%lu records=%u seq=%lu first_point=%u first_type=%u%s",
                 static_cast<unsigned long>(g_frame_count),
                 static_cast<unsigned>(prefix.update_count),
                 static_cast<unsigned long>(prefix.states_sequence),
                 static_cast<unsigned>(first_record.point_index),
                 static_cast<unsigned>(first_record.value_type),
                 context);
    }
    return ESP_OK;
}

esp_err_t handle_unsolicited_response()
{
    uint8_t payload[kMaxMailboxPayload + 1] = {};
    uint16_t payload_len = 0u;
    ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                        kLogTag,
                        "read unsolicited response failed");
    ESP_RETURN_ON_ERROR(finalize_response_read("unsolicited"),
                        kLogTag,
                        "finalize unsolicited response failed");

    if (payload_len < plclink::kHeaderSize) {
        ESP_LOGW(kLogTag,
                 "Discarded short unsolicited mailbox response len=%u",
                 static_cast<unsigned>(payload_len));
        return ESP_OK;
    }

    plclink::ProtocolHeader header = {};
    if (!plclink::decodeHeader(header, payload, payload_len)) {
        ESP_LOGW(kLogTag, "Discarded invalid unsolicited plcLink header");
        return ESP_OK;
    }

    if (kEnableMailboxTraceLogs) {
        ESP_LOGI(kLogTag,
                 "RX unsolicited mailbox len=%u type=0x%02x gen=%lu seq=%lu flags=0x%02x",
                 static_cast<unsigned>(payload_len),
                 static_cast<unsigned>(header.message_type),
                 static_cast<unsigned long>(header.generation),
                 static_cast<unsigned long>(header.sequence),
                 static_cast<unsigned>(header.flags));
    }

    if (g_caps_response_seen && header.generation != g_defs_generation) {
        ESP_LOGW(kLogTag,
                 "plcLink defs generation changed during unsolicited update local=%lu remote=%lu, requesting full resync",
                 static_cast<unsigned long>(g_defs_generation),
                 static_cast<unsigned long>(header.generation));
        begin_full_resync(header.generation);
        return ESP_OK;
    }

    if (header.message_type != plclink::kMsgStatesUpdatesRes) {
        if (kEnableMailboxTraceLogs) {
            ESP_LOGW(kLogTag,
                     "Discarded unsolicited plcLink response type=0x%02x len=%u",
                     static_cast<unsigned>(header.message_type),
                     static_cast<unsigned>(payload_len));
        }
        return ESP_OK;
    }

    return process_state_update_frame(payload, payload_len, "");
}

uint16_t compute_max_state_records()
{
    if (g_max_fragment_payload < sizeof(plclink::StatesSnapshotChunkPrefix)) {
        return 0u;
    }

    const size_t available = static_cast<size_t>(g_max_fragment_payload) -
                             sizeof(plclink::StatesSnapshotChunkPrefix);
    return static_cast<uint16_t>(available / sizeof(plclink::StateRecordV1));
}

bool is_snapshot_in_progress()
{
    return g_caps_response_seen && (!g_defs_response_seen || !g_states_response_seen);
}

bool is_runtime_idle_ready()
{
    return g_defs_response_seen &&
           g_states_response_seen &&
           !g_caps_request_sent &&
           !g_defs_request_sent &&
           !g_states_request_sent;
}

bool can_send_request(uint16_t status)
{
    return (status & 0x0001u) == 0u;
}

uint8_t compute_snapshot_percent()
{
    if (!g_caps_response_seen) {
        return kBootPercentLinkReady;
    }

    if (g_defs_response_seen && g_states_response_seen) {
        return 100u;
    }

    if ((g_point_count == 0u) || ((!g_defs_request_sent) && !g_defs_response_seen && (g_defs_loaded_bytes == 0u))) {
        return kBootPercentCapsReady;
    }

    if (!g_defs_response_seen) {
        const uint32_t total = (g_defs_total_bytes == 0u) ? 1u : g_defs_total_bytes;
        const uint32_t completed = (g_defs_loaded_bytes > g_defs_total_bytes) ? g_defs_total_bytes : g_defs_loaded_bytes;
        const uint32_t scaled = (completed * (kBootPercentSnapshotSpan / 2u)) / total;
        return static_cast<uint8_t>(kBootPercentSnapshotBase + scaled);
    }

    const uint32_t completed = static_cast<uint32_t>(g_snapshot_record_count);
    const uint32_t total = (g_point_count == 0u) ? 1u : static_cast<uint32_t>(g_point_count);
    const uint32_t scaled = (completed * (kBootPercentSnapshotSpan / 2u)) / total;
    const uint32_t percent = static_cast<uint32_t>(kBootPercentSnapshotBase) + (kBootPercentSnapshotSpan / 2u) + scaled;
    return static_cast<uint8_t>((percent >= 100u) ? 99u : percent);
}

constexpr uint16_t status_bit(uint16_t status, uint8_t bit)
{
    return static_cast<uint16_t>((status >> bit) & 0x1u);
}

esp_err_t read_status(uint16_t* out_status, uint8_t* out_rx)
{
    if ((g_fpga_device == nullptr) || (out_status == nullptr) || (out_rx == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t transaction = {};
    transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    transaction.length = 3u * 8u;
    transaction.tx_data[0] = kOpcodeReadStatus;
    transaction.tx_data[1] = 0x00;
    transaction.tx_data[2] = 0x00;

    ESP_RETURN_ON_ERROR(spi_device_transmit(g_fpga_device, &transaction),
                        kLogTag,
                        "status spi_device_transmit failed");

    out_rx[0] = transaction.rx_data[0];
    out_rx[1] = transaction.rx_data[1];
    out_rx[2] = transaction.rx_data[2];
    *out_status = static_cast<uint16_t>(out_rx[1]) |
                  (static_cast<uint16_t>(out_rx[2]) << 8);
    return ESP_OK;
}

esp_err_t recover_startup_mailbox(uint16_t status, int irq_level)
{
    const bool has_mailbox_state = (status_bit(status, 0) != 0u) ||
                                   (status_bit(status, 4) != 0u) ||
                                   (status_bit(status, 6) != 0u);
    if (!has_mailbox_state) {
        if (irq_level != 0 && !g_startup_irq_inconsistent_logged) {
            ESP_LOGW(kLogTag,
                     "Ignoring startup IRQ high with empty STATUS=0x%04x; bridge not ready or IRQ line floating",
                     static_cast<unsigned>(status));
            g_startup_irq_inconsistent_logged = true;
        }
        return ESP_OK;
    }

    ESP_LOGW(kLogTag,
             "Resetting stale startup mailbox status=0x%04x irq=%d rx_ready=%u tx_loaded=%u tx_ready_for_esp32=%u",
             static_cast<unsigned>(status),
             irq_level,
             static_cast<unsigned>(status_bit(status, 0)),
             static_cast<unsigned>(status_bit(status, 4)),
             static_cast<unsigned>(status_bit(status, 5)));

    ESP_RETURN_ON_ERROR(write_control(kControlResetMailbox),
                        kLogTag,
                        "startup mailbox reset failed");
    begin_full_resync(0u);
    return ESP_OK;
}

esp_err_t write_request(const uint8_t* payload, uint16_t payload_len)
{
    if ((g_fpga_device == nullptr) || ((payload_len != 0u) && (payload == nullptr))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (payload_len > kMaxMailboxPayload) {
        return ESP_ERR_INVALID_SIZE;
    }

    std::memset(g_mailbox_tx_buffer, 0, sizeof(g_mailbox_tx_buffer));
    g_mailbox_tx_buffer[0] = kOpcodeWriteRequest;
    g_mailbox_tx_buffer[1] = static_cast<uint8_t>(payload_len & 0xFFu);
    g_mailbox_tx_buffer[2] = static_cast<uint8_t>((payload_len >> 8) & 0xFFu);
    if (payload_len != 0u) {
        std::memcpy(&g_mailbox_tx_buffer[3], payload, payload_len);
    }

    spi_transaction_t transaction = {};
    transaction.length = static_cast<size_t>(3u + payload_len) * 8u;
    transaction.tx_buffer = g_mailbox_tx_buffer;

    return spi_device_transmit(g_fpga_device, &transaction);
}

esp_err_t write_control(uint16_t control_word)
{
    if (g_fpga_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t transaction = {};
    transaction.flags = SPI_TRANS_USE_TXDATA;
    transaction.length = 3u * 8u;
    transaction.tx_data[0] = kOpcodeWriteControl;
    transaction.tx_data[1] = static_cast<uint8_t>(control_word & 0xFFu);
    transaction.tx_data[2] = static_cast<uint8_t>((control_word >> 8) & 0xFFu);

    return spi_device_transmit(g_fpga_device, &transaction);
}

esp_err_t finalize_response_read(const char* context)
{
    ESP_RETURN_ON_ERROR(write_control(kControlClearIrq),
                        kLogTag,
                        "%s clear_irq failed",
                        context);

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        uint8_t status_rx[3] = {};
        uint16_t status = 0u;
        ESP_RETURN_ON_ERROR(read_status(&status, status_rx),
                            kLogTag,
                            "%s read_status after clear failed",
                            context);

        if (status_bit(status, 4) == 0u && status_bit(status, 6) == 0u) {
            return ESP_OK;
        }

        if (kEnableMailboxTraceLogs) {
            ESP_LOGW(kLogTag,
                     "%s mailbox still loaded after clear attempt=%u status=0x%04x irq=%u tx_loaded=%u tx_ready_for_esp32=%u",
                     context,
                     static_cast<unsigned>(attempt + 1u),
                     static_cast<unsigned>(status),
                     static_cast<unsigned>(status_bit(status, 6)),
                     static_cast<unsigned>(status_bit(status, 4)),
                     static_cast<unsigned>(status_bit(status, 5)));
        }

        ESP_RETURN_ON_ERROR(write_control(kControlClearIrq),
                            kLogTag,
                            "%s repeated clear_irq failed",
                            context);
    }

    return ESP_OK;
}

esp_err_t read_response(uint8_t* out_payload, uint16_t capacity, uint16_t* out_payload_len)
{
    if ((g_fpga_device == nullptr) || (out_payload_len == nullptr) || ((capacity != 0u) && (out_payload == nullptr))) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t length_transaction = {};
    length_transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    length_transaction.length = 3u * 8u;
    length_transaction.tx_data[0] = kOpcodeReadResponse;
    length_transaction.tx_data[1] = 0x00;
    length_transaction.tx_data[2] = 0x00;
    ESP_RETURN_ON_ERROR(spi_device_transmit(g_fpga_device, &length_transaction),
                        kLogTag,
                        "response length read failed");

    const uint16_t payload_len = static_cast<uint16_t>(length_transaction.rx_data[1]) |
                                 (static_cast<uint16_t>(length_transaction.rx_data[2]) << 8);
    *out_payload_len = payload_len;
    if (payload_len > capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (payload_len == 0u) {
        return ESP_OK;
    }

    std::memset(g_mailbox_tx_buffer, 0, sizeof(g_mailbox_tx_buffer));
    std::memset(g_mailbox_rx_buffer, 0, sizeof(g_mailbox_rx_buffer));
    g_mailbox_tx_buffer[0] = kOpcodeReadResponse;

    spi_transaction_t payload_transaction = {};
    payload_transaction.length = static_cast<size_t>(3u + payload_len) * 8u;
    payload_transaction.tx_buffer = g_mailbox_tx_buffer;
    payload_transaction.rx_buffer = g_mailbox_rx_buffer;
    ESP_RETURN_ON_ERROR(spi_device_transmit(g_fpga_device, &payload_transaction),
                        kLogTag,
                        "response payload read failed");

    std::memcpy(out_payload, &g_mailbox_rx_buffer[3], payload_len);
    return ESP_OK;
}

uint8_t next_request_id()
{
    const uint8_t request_id = g_next_request_id;
    ++g_next_request_id;
    if (g_next_request_id == 0u) {
        g_next_request_id = 1u;
    }
    return request_id;
}

esp_err_t send_states_snapshot_request(uint16_t start_index)
{
    uint8_t request_buffer[plclink::kHeaderSize + sizeof(plclink::GetStatesSnapshotRequest)] = {};
    plclink::ProtocolHeader header = {};
    header.magic = plclink::kMagic;
    header.version = plclink::kVersion;
    header.message_type = plclink::kMsgGetStatesSnapshotReq;
    header.flags = static_cast<uint8_t>(plclink::kFlagRequest | plclink::kFlagSnapshotPayload);
    header.request_id = next_request_id();
    header.fragment_index = 0u;
    header.fragment_count = 1u;
    header.payload_length = static_cast<uint16_t>(sizeof(plclink::GetStatesSnapshotRequest));
    if (!plclink::encodeHeader(header, request_buffer, sizeof(request_buffer))) {
        return ESP_ERR_INVALID_ARG;
    }

    plclink::GetStatesSnapshotRequest request = {};
    request.start_index = start_index;
    request.max_records = compute_max_state_records();
    if (request.max_records == 0u) {
        ESP_LOGE(kLogTag,
                 "plcLink max fragment payload too small for states snapshot: %u",
                 static_cast<unsigned>(g_max_fragment_payload));
        return ESP_ERR_INVALID_SIZE;
    }
    request.include_strings = 1u;
    std::memcpy(&request_buffer[plclink::kHeaderSize], &request, sizeof(request));

    ESP_RETURN_ON_ERROR(write_request(request_buffer, sizeof(request_buffer)),
                        kLogTag,
                        "write states snapshot request failed");
    g_states_request_sent = true;
    if (start_index == 0u) {
        g_snapshot_chunk_count = 0u;
        g_snapshot_record_count = 0u;
        reset_state_cache(g_point_count);
        ESP_LOGI(kLogTag,
                 "TX plcLink states snapshot start len=%u request_id=%u max_records=%u point_count=%u max_payload=%u",
                 static_cast<unsigned>(sizeof(request_buffer)),
                 static_cast<unsigned>(header.request_id),
                 static_cast<unsigned>(request.max_records),
                 static_cast<unsigned>(g_point_count),
                 static_cast<unsigned>(g_max_fragment_payload));
    }
    return ESP_OK;
}

esp_err_t send_defs_snapshot_request(uint32_t resume_offset)
{
    uint8_t request_buffer[plclink::kHeaderSize + sizeof(plclink::GetDefsSnapshotRequest)] = {};
    plclink::ProtocolHeader header = {};
    header.magic = plclink::kMagic;
    header.version = plclink::kVersion;
    header.message_type = plclink::kMsgGetDefsSnapshotReq;
    header.flags = static_cast<uint8_t>(plclink::kFlagRequest | plclink::kFlagSnapshotPayload);
    header.request_id = next_request_id();
    header.fragment_index = 0u;
    header.fragment_count = 1u;
    header.payload_length = static_cast<uint16_t>(sizeof(plclink::GetDefsSnapshotRequest));
    if (!plclink::encodeHeader(header, request_buffer, sizeof(request_buffer))) {
        return ESP_ERR_INVALID_ARG;
    }

    plclink::GetDefsSnapshotRequest request = {};
    request.resume_offset = resume_offset;
    request.max_chunk_payload = compute_max_defs_payload_bytes();
    request.requested_generation = g_defs_generation;
    if (request.max_chunk_payload == 0u) {
        ESP_LOGE(kLogTag,
                 "plcLink max fragment payload too small for defs snapshot: %u",
                 static_cast<unsigned>(g_max_fragment_payload));
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(&request_buffer[plclink::kHeaderSize], &request, sizeof(request));

    ESP_RETURN_ON_ERROR(write_request(request_buffer, sizeof(request_buffer)),
                        kLogTag,
                        "write defs snapshot request failed");
    g_defs_request_sent = true;
    if (resume_offset == 0u) {
        reset_defs_cache();
    }
    return ESP_OK;
}

esp_err_t add_device(gpio_num_t cs_pin, spi_device_handle_t* out_handle)
{
    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = app_config::kSpiClockHz;
    device_config.mode = 0;
    device_config.spics_io_num = cs_pin;
    device_config.queue_size = app_config::kSpiQueueSize;
    device_config.command_bits = 0;
    device_config.address_bits = 0;
    device_config.dummy_bits = 0;

    return spi_bus_add_device(app_config::kSpiHost, &device_config, out_handle);
}

} // namespace

esp_err_t init()
{
    if (g_fpga_device != nullptr) {
        return ESP_OK;
    }

    if (g_state_mutex == nullptr) {
        g_state_mutex = xSemaphoreCreateMutex();
        if (g_state_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t irq_config = {};
    irq_config.pin_bit_mask = (1ULL << static_cast<uint32_t>(app_config::kFpgaIrq));
    irq_config.mode = GPIO_MODE_INPUT;
    irq_config.pull_up_en = GPIO_PULLUP_DISABLE;
    irq_config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    irq_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&irq_config));

    ESP_RETURN_ON_ERROR(spi_bus_shared::ensure_bus(),
                        kLogTag,
                        "shared spi_bus_initialize failed");

    ESP_RETURN_ON_ERROR(add_device(app_config::kFpgaCs, &g_fpga_device),
                        kLogTag,
                        "spi_bus_add_device(fpga) failed");

    {
        const int irq_level = gpio_get_level(app_config::kFpgaIrq);
        uint8_t status_rx[3] = {};
        uint16_t status = 0u;
        ESP_RETURN_ON_ERROR(read_status(&status, status_rx),
                            kLogTag,
                            "startup read_status failed");
        ESP_RETURN_ON_ERROR(recover_startup_mailbox(status, irq_level),
                            kLogTag,
                            "startup mailbox sanitize failed");
    }

    ESP_LOGI(kLogTag,
             "SPI ready host=%d sck=%d mosi=%d miso=%d fpga_cs=%d irq=%d mode=%d hz=%d max_transfer=%d irq_level=%d",
             static_cast<int>(app_config::kSpiHost),
             static_cast<int>(app_config::kSpiSck),
             static_cast<int>(app_config::kSpiMosi),
             static_cast<int>(app_config::kSpiMiso),
             static_cast<int>(app_config::kFpgaCs),
             static_cast<int>(app_config::kFpgaIrq),
             0,
             app_config::kSpiClockHz,
             app_config::kSpiMaxTransferBytes,
             gpio_get_level(app_config::kFpgaIrq));
    return ESP_OK;
}

esp_err_t poll()
{
    if (g_fpga_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const int irq_level = gpio_get_level(app_config::kFpgaIrq);
    uint8_t status_rx[3] = {};
    uint16_t status = 0;
    ++g_poll_count;
    if (kEnableMailboxTraceLogs && (irq_level != g_last_irq_level) && !is_snapshot_in_progress()) {
        ESP_LOGI(kLogTag,
                 "IRQ level changed to %d after %lu polls",
                 irq_level,
                 static_cast<unsigned long>(g_poll_count));
    }
    g_last_irq_level = irq_level;

    ESP_RETURN_ON_ERROR(read_status(&status, status_rx),
                        kLogTag,
                        "read_status failed");

    if (kEnableMailboxTraceLogs && ((status != g_last_status) || (g_poll_count == 1u)) && !is_snapshot_in_progress()) {
        ESP_LOGI(kLogTag,
                 "STATUS raw=%02x %02x %02x decoded=0x%04x rx_ready=%u rx_overflow=%u rx_frame_error=%u tx_ready_for_cpu=%u tx_loaded=%u tx_ready_for_esp32=%u irq_asserted=%u spi_active=%u rx_in_progress=%u tx_in_progress=%u",
                 status_rx[0],
                 status_rx[1],
                 status_rx[2],
                 static_cast<unsigned>(status),
                 static_cast<unsigned>(status_bit(status, 0)),
                 static_cast<unsigned>(status_bit(status, 1)),
                 static_cast<unsigned>(status_bit(status, 2)),
                 static_cast<unsigned>(status_bit(status, 3)),
                 static_cast<unsigned>(status_bit(status, 4)),
                 static_cast<unsigned>(status_bit(status, 5)),
                 static_cast<unsigned>(status_bit(status, 6)),
                 static_cast<unsigned>(status_bit(status, 7)),
                 static_cast<unsigned>(status_bit(status, 8)),
                 static_cast<unsigned>(status_bit(status, 9)));
    }
    g_last_status = status;

    if (!g_caps_request_sent && !g_caps_response_seen) {
        ESP_RETURN_ON_ERROR(recover_startup_mailbox(status, irq_level),
                            kLogTag,
                            "startup mailbox recovery failed");
        if (g_resync_pending) {
            return ESP_OK;
        }
    }

    if (g_resync_pending && !g_caps_request_sent && can_send_request(status)) {
        g_resync_pending = false;
        uint8_t request_buffer[plclink::kHeaderSize] = {};
        plclink::ProtocolHeader header = {};
        header.magic = plclink::kMagic;
        header.version = plclink::kVersion;
        header.message_type = plclink::kMsgGetCapsReq;
        header.flags = static_cast<uint8_t>(plclink::kFlagRequest);
        header.request_id = next_request_id();
        header.fragment_index = 0u;
        header.fragment_count = 1u;
        header.payload_length = 0u;
        if (!plclink::encodeHeader(header, request_buffer, sizeof(request_buffer))) {
            return ESP_ERR_INVALID_ARG;
        }

        ESP_RETURN_ON_ERROR(write_request(request_buffer, sizeof(request_buffer)),
                            kLogTag,
                            "write_request failed during resync");
        g_caps_request_sent = true;
        ESP_LOGI(kLogTag,
                 "TX plcLink get_caps request for resync len=%u request_id=%u",
                 static_cast<unsigned>(sizeof(request_buffer)),
                 static_cast<unsigned>(header.request_id));
        return ESP_OK;
    }

    if (g_write_state_request_sent && (status_bit(status, 5) != 0u)) {
        uint8_t payload[kMaxMailboxPayload + 1] = {};
        uint16_t payload_len = 0u;
        ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                            kLogTag,
                            "read write_state response failed");
        ESP_RETURN_ON_ERROR(finalize_response_read("write_state"),
                            kLogTag,
                            "finalize write_state response failed");

        if (payload_len < plclink::kHeaderSize) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        plclink::ProtocolHeader header = {};
        if (!plclink::decodeHeader(header, payload, payload_len)) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        g_write_state_request_sent = false;
        g_pending_write_state = {};

        if (header.message_type == plclink::kMsgWriteStateRes) {
            if (payload_len < (plclink::kHeaderSize + sizeof(plclink::WriteStateResponseV1))) {
                return ESP_ERR_INVALID_RESPONSE;
            }

            plclink::WriteStateResponseV1 response = {};
            std::memcpy(&response, &payload[plclink::kHeaderSize], sizeof(response));
            if (response.status_code != plclink::kErrorOk) {
                ESP_LOGW(kLogTag,
                         "RX plcLink write_state rejected code=%u type=%u seq=%lu",
                         static_cast<unsigned>(response.status_code),
                         static_cast<unsigned>(response.applied_value_type),
                         static_cast<unsigned long>(response.result_sequence));
            }
            return ESP_OK;
        }

        if (header.message_type == plclink::kMsgStatesUpdatesRes) {
            return process_state_update_frame(payload, payload_len, " while waiting for write_state");
        }

        if (header.message_type == plclink::kMsgError) {
            return ESP_OK;
        }

        return ESP_OK;
    }

    if (is_runtime_idle_ready() && (status_bit(status, 5) != 0u)) {
        ++g_runtime_status5_ready_count;
        if (kEnableMailboxTraceLogs) {
            ESP_LOGI(kLogTag,
                     "Runtime mailbox response-ready #%lu status=0x%04x irq=%d",
                     static_cast<unsigned long>(g_runtime_status5_ready_count),
                     static_cast<unsigned>(status),
                     irq_level);
        }
        return handle_unsolicited_response();
    }

    if (g_caps_response_seen && !g_defs_request_sent && !g_defs_response_seen && (status_bit(status, 5) != 0u)) {
        return handle_unsolicited_response();
    }

    if (g_defs_response_seen && !g_states_request_sent && !g_states_response_seen && (status_bit(status, 5) != 0u)) {
        return handle_unsolicited_response();
    }

    if (g_defs_response_seen && g_states_response_seen &&
        !g_caps_request_sent && !g_defs_request_sent && !g_states_request_sent &&
        (status_bit(status, 5) != 0u)) {
        ++g_runtime_status5_ready_count;
        if (kEnableMailboxTraceLogs) {
            ESP_LOGI(kLogTag,
                     "Runtime mailbox response-ready #%lu status=0x%04x irq=%d",
                     static_cast<unsigned long>(g_runtime_status5_ready_count),
                     static_cast<unsigned>(status),
                     irq_level);
        }
        return handle_unsolicited_response();
    }

    if (is_runtime_idle_ready() &&
        (status_bit(status, 5) == 0u) &&
        ((irq_level != 0) || (status_bit(status, 6) != 0u) || (status_bit(status, 4) != 0u))) {
        ++g_runtime_suspicious_mailbox_count;
        if (kEnableMailboxTraceLogs &&
            (g_runtime_suspicious_mailbox_count == 1u ||
             (g_runtime_suspicious_mailbox_count % 128u) == 0u)) {
            ESP_LOGW(kLogTag,
                     "Runtime mailbox suspicious #%lu status=0x%04x irq=%d rx_ready=%u tx_ready_for_cpu=%u tx_loaded=%u tx_ready_for_esp32=%u",
                     static_cast<unsigned long>(g_runtime_suspicious_mailbox_count),
                     static_cast<unsigned>(status),
                     irq_level,
                     static_cast<unsigned>(status_bit(status, 0)),
                     static_cast<unsigned>(status_bit(status, 3)),
                     static_cast<unsigned>(status_bit(status, 4)),
                     static_cast<unsigned>(status_bit(status, 5)));
        }
    }

    if (g_refresh_requested && g_caps_response_seen && g_states_response_seen && !g_states_request_sent && can_send_request(status)) {
        g_refresh_requested = false;
        g_states_response_seen = false;
        g_next_states_start_index = 0u;
        return send_states_snapshot_request(0u);
    }

    if (g_pending_write_state.valid &&
        !g_write_state_request_sent &&
        is_runtime_idle_ready() &&
        can_send_request(status)) {
        return send_write_state_request(g_pending_write_state);
    }

    if (!g_caps_request_sent && !g_caps_response_seen && can_send_request(status)) {
        uint8_t request_buffer[plclink::kHeaderSize] = {};
        plclink::ProtocolHeader header = {};
        header.magic = plclink::kMagic;
        header.version = plclink::kVersion;
        header.message_type = plclink::kMsgGetCapsReq;
        header.flags = static_cast<uint8_t>(plclink::kFlagRequest);
        header.request_id = next_request_id();
        header.fragment_index = 0u;
        header.fragment_count = 1u;
        header.payload_length = 0u;
        if (!plclink::encodeHeader(header, request_buffer, sizeof(request_buffer))) {
            return ESP_ERR_INVALID_ARG;
        }

        ESP_RETURN_ON_ERROR(write_request(request_buffer, sizeof(request_buffer)),
                            kLogTag,
                            "write_request failed");
        g_caps_request_sent = true;
        ESP_LOGI(kLogTag,
                 "TX plcLink get_caps request len=%u request_id=%u",
                 static_cast<unsigned>(sizeof(request_buffer)),
                 static_cast<unsigned>(header.request_id));
        return ESP_OK;
    }

    if (g_caps_request_sent && !g_caps_response_seen && (status_bit(status, 5) != 0u)) {
        uint8_t payload[kMaxMailboxPayload + 1] = {};
        uint16_t payload_len = 0;
        ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                            kLogTag,
                            "read_response failed");
        ESP_RETURN_ON_ERROR(finalize_response_read("caps"),
                    kLogTag,
                    "finalize caps response failed");

        if (payload_len < plclink::kHeaderSize) {
            ESP_LOGE(kLogTag, "plcLink response too short len=%u", static_cast<unsigned>(payload_len));
            return ESP_ERR_INVALID_RESPONSE;
        }

        plclink::ProtocolHeader header = {};
        if (!plclink::decodeHeader(header, payload, payload_len)) {
            ESP_LOGE(kLogTag, "plcLink response header invalid");
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (header.message_type == plclink::kMsgGetCapsRes) {
            if (payload_len < (plclink::kHeaderSize + sizeof(plclink::CapsResponsePayload))) {
                ESP_LOGE(kLogTag,
                         "plcLink caps response payload too short len=%u",
                         static_cast<unsigned>(payload_len));
                return ESP_ERR_INVALID_RESPONSE;
            }

            ++g_frame_count;
            g_caps_request_sent = false;
            g_caps_response_seen = true;
            plclink::CapsResponsePayload caps = {};
            std::memcpy(&caps, &payload[plclink::kHeaderSize], sizeof(caps));
            g_point_count = caps.point_count;
            g_max_fragment_payload = caps.max_fragment_payload;
            g_defs_generation = caps.defs_generation;
            g_states_sequence = caps.states_sequence;
            g_defs_request_sent = false;
            g_defs_response_seen = false;
            g_next_defs_offset = 0u;
            g_resync_pending = false;
            reset_state_cache(g_point_count);
            reset_defs_cache();
            ESP_LOGI(kLogTag,
                     "RX plcLink caps #%lu protocol=%u caps=0x%04x max_payload=%u point_count=%u defs_generation=%lu states_sequence=%lu",
                     static_cast<unsigned long>(g_frame_count),
                     static_cast<unsigned>(caps.protocol_version),
                     static_cast<unsigned>(caps.capability_flags),
                     static_cast<unsigned>(caps.max_fragment_payload),
                     static_cast<unsigned>(caps.point_count),
                     static_cast<unsigned long>(caps.defs_generation),
                     static_cast<unsigned long>(caps.states_sequence));
        } else if (header.message_type == plclink::kMsgError) {
            ++g_frame_count;
            if (payload_len >= (plclink::kHeaderSize + sizeof(plclink::ErrorPayloadV1))) {
                plclink::ErrorPayloadV1 error_payload = {};
                std::memcpy(&error_payload, &payload[plclink::kHeaderSize], sizeof(error_payload));
                ESP_LOGE(kLogTag,
                         "RX plcLink error #%lu code=%u related=0x%02x detail=%lu",
                         static_cast<unsigned long>(g_frame_count),
                         static_cast<unsigned>(error_payload.error_code),
                         static_cast<unsigned>(error_payload.related_message_type),
                         static_cast<unsigned long>(error_payload.detail));
            } else {
                ESP_LOGE(kLogTag,
                         "RX plcLink error #%lu with short payload len=%u",
                         static_cast<unsigned long>(g_frame_count),
                         static_cast<unsigned>(payload_len));
            }
        } else if (header.message_type == plclink::kMsgStatesUpdatesRes) {
            plclink::StatesUpdatePrefix prefix = {};
            if (payload_len < (plclink::kHeaderSize + sizeof(prefix))) {
                ESP_LOGW(kLogTag,
                         "Ignored short plcLink state update while waiting for caps len=%u",
                         static_cast<unsigned>(payload_len));
                return ESP_OK;
            }

            std::memcpy(&prefix, &payload[plclink::kHeaderSize], sizeof(prefix));
            const uint8_t* chunk_bytes = &payload[plclink::kHeaderSize + sizeof(prefix)];
            const size_t chunk_payload_bytes = header.payload_length - sizeof(prefix);
            size_t expected_records_payload = 0u;
            if (!measure_state_records_payload(chunk_bytes,
                                               chunk_payload_bytes,
                                               prefix.update_count,
                                               &expected_records_payload)) {
                ESP_LOGW(kLogTag,
                         "Ignored malformed plcLink state update while waiting for caps");
                return ESP_OK;
            }

            const size_t expected_payload = sizeof(prefix) + expected_records_payload;
            if (header.payload_length < expected_payload || payload_len < (plclink::kHeaderSize + expected_payload)) {
                ESP_LOGW(kLogTag,
                         "Ignored truncated plcLink state update while waiting for caps");
                return ESP_OK;
            }

            ++g_frame_count;
            store_update_chunk(prefix, chunk_bytes, expected_records_payload);
            if (kEnableMailboxTraceLogs) {
                ESP_LOGI(kLogTag,
                         "RX plcLink state update #%lu records=%u seq=%lu while waiting for caps",
                         static_cast<unsigned long>(g_frame_count),
                         static_cast<unsigned>(prefix.update_count),
                         static_cast<unsigned long>(prefix.states_sequence));
            }
        } else {
            ESP_LOGW(kLogTag,
                     "Unexpected plcLink response while waiting for caps type=0x%02x len=%u",
                     static_cast<unsigned>(header.message_type),
                     static_cast<unsigned>(payload_len));
        }
        return ESP_OK;
    }

    if (g_caps_response_seen && !g_defs_request_sent && !g_defs_response_seen && can_send_request(status)) {
        return send_defs_snapshot_request(g_next_defs_offset);
    }

    if (g_defs_request_sent && !g_defs_response_seen && (status_bit(status, 5) != 0u)) {
        uint8_t payload[kMaxMailboxPayload + 1] = {};
        uint16_t payload_len = 0;
        ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                            kLogTag,
                            "read defs response failed");
        ESP_RETURN_ON_ERROR(finalize_response_read("defs"),
                    kLogTag,
                    "finalize defs response failed");

        if (payload_len < (plclink::kHeaderSize + sizeof(plclink::DefsSnapshotChunkPrefix))) {
            ESP_LOGE(kLogTag, "plcLink defs response too short len=%u", static_cast<unsigned>(payload_len));
            return ESP_ERR_INVALID_RESPONSE;
        }

        plclink::ProtocolHeader header = {};
        if (!plclink::decodeHeader(header, payload, payload_len)) {
            ESP_LOGE(kLogTag, "plcLink defs response header invalid");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (header.message_type == plclink::kMsgStatesUpdatesRes) {
            g_defs_request_sent = false;
            return process_state_update_frame(payload, payload_len, " while waiting for defs snapshot");
        }
        if (header.message_type != plclink::kMsgDefsSnapshotRes) {
            ESP_LOGW(kLogTag,
                     "Unexpected plcLink defs response type=0x%02x len=%u",
                     static_cast<unsigned>(header.message_type),
                     static_cast<unsigned>(payload_len));
            return ESP_OK;
        }

        plclink::DefsSnapshotChunkPrefix prefix = {};
        std::memcpy(&prefix, &payload[plclink::kHeaderSize], sizeof(prefix));
        const size_t expected_payload = sizeof(prefix) + prefix.chunk_payload_bytes;
        if (header.payload_length < expected_payload || payload_len < (plclink::kHeaderSize + expected_payload)) {
            ESP_LOGE(kLogTag,
                     "plcLink defs payload truncated payload_len=%u expected=%u",
                     static_cast<unsigned>(header.payload_length),
                     static_cast<unsigned>(expected_payload));
            return ESP_ERR_INVALID_RESPONSE;
        }

        ++g_frame_count;
        g_defs_request_sent = false;
        store_defs_chunk(prefix, &payload[plclink::kHeaderSize + sizeof(prefix)]);
        g_next_defs_offset = prefix.chunk_offset + prefix.chunk_payload_bytes;
        if (prefix.more == 0u) {
            g_defs_response_seen = true;
            ESP_LOGI(kLogTag,
                     "Completed plcLink defs snapshot points=%u bytes=%lu generation=%lu",
                     static_cast<unsigned>(prefix.total_point_count),
                     static_cast<unsigned long>(prefix.total_serialized_bytes),
                     static_cast<unsigned long>(prefix.defs_generation));
        }
        return ESP_OK;
    }

    if (g_defs_response_seen && !g_states_request_sent && !g_states_response_seen && can_send_request(status)) {
        return send_states_snapshot_request(g_next_states_start_index);
    }

    if (g_states_request_sent && !g_states_response_seen && (status_bit(status, 5) != 0u)) {
        uint8_t payload[kMaxMailboxPayload + 1] = {};
        uint16_t payload_len = 0;
        ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                            kLogTag,
                            "read states response failed");
        ESP_RETURN_ON_ERROR(finalize_response_read("states"),
                    kLogTag,
                    "finalize states response failed");

        if (payload_len < (plclink::kHeaderSize + sizeof(plclink::StatesSnapshotChunkPrefix))) {
            ESP_LOGE(kLogTag, "plcLink states response too short len=%u", static_cast<unsigned>(payload_len));
            return ESP_ERR_INVALID_RESPONSE;
        }

        plclink::ProtocolHeader header = {};
        if (!plclink::decodeHeader(header, payload, payload_len)) {
            ESP_LOGE(kLogTag, "plcLink states response header invalid");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (header.message_type == plclink::kMsgStatesUpdatesRes) {
            g_states_request_sent = false;
            return process_state_update_frame(payload, payload_len, " while waiting for states snapshot");
        }
        if (header.message_type != plclink::kMsgStatesSnapshotRes) {
            ESP_LOGW(kLogTag,
                     "Unexpected plcLink states response type=0x%02x len=%u",
                     static_cast<unsigned>(header.message_type),
                     static_cast<unsigned>(payload_len));
            return ESP_OK;
        }

        plclink::StatesSnapshotChunkPrefix prefix = {};
        std::memcpy(&prefix, &payload[plclink::kHeaderSize], sizeof(prefix));
        const uint8_t* chunk_bytes = &payload[plclink::kHeaderSize + sizeof(prefix)];
        const size_t chunk_payload_bytes = header.payload_length - sizeof(prefix);

        size_t expected_records_payload = 0u;
        if (!measure_state_records_payload(chunk_bytes,
                                           chunk_payload_bytes,
                                           prefix.returned_record_count,
                                           &expected_records_payload)) {
            ESP_LOGE(kLogTag,
                     "plcLink states payload truncated while scanning %u records",
                     static_cast<unsigned>(prefix.returned_record_count));
            return ESP_ERR_INVALID_RESPONSE;
        }

        const size_t expected_payload = sizeof(prefix) + expected_records_payload;
        if (header.payload_length < expected_payload || payload_len < (plclink::kHeaderSize + expected_payload)) {
            ESP_LOGE(kLogTag,
                     "plcLink states payload truncated payload_len=%u expected=%u",
                     static_cast<unsigned>(header.payload_length),
                     static_cast<unsigned>(expected_payload));
            return ESP_ERR_INVALID_RESPONSE;
        }

        ++g_frame_count;
        ++g_snapshot_chunk_count;
        g_snapshot_record_count = static_cast<uint16_t>(g_snapshot_record_count + prefix.returned_record_count);
        store_snapshot_chunk(prefix, chunk_bytes, expected_records_payload);

        g_states_request_sent = false;
        if ((prefix.more != 0u) && (prefix.returned_record_count == 0u)) {
            ESP_LOGE(kLogTag,
                     "plcLink states snapshot reported more data with zero records at start=%u",
                     static_cast<unsigned>(prefix.returned_start_index));
            return ESP_ERR_INVALID_RESPONSE;
        }

        g_next_states_start_index = static_cast<uint16_t>(prefix.returned_start_index + prefix.returned_record_count);
        if (prefix.more != 0u) {
        } else {
            g_states_response_seen = true;
            ESP_LOGI(kLogTag,
                     "Completed plcLink states snapshot total=%u chunks=%u records=%u seq=%lu",
                     static_cast<unsigned>(prefix.total_point_count),
                     static_cast<unsigned>(g_snapshot_chunk_count),
                     static_cast<unsigned>(g_snapshot_record_count),
                     static_cast<unsigned long>(prefix.states_sequence_base));
        }
    }

    return ESP_OK;
}

bool is_busy()
{
    return (g_fpga_device != nullptr) &&
           (!g_caps_response_seen || !g_defs_response_seen || !g_states_response_seen ||
            g_caps_request_sent || g_defs_request_sent || g_states_request_sent ||
            g_refresh_requested || g_write_state_request_sent || g_pending_write_state.valid);
}

BootProgress get_boot_progress()
{
    BootProgress progress = {};
    progress.percent = 10u;
    progress.loaded_points = g_snapshot_record_count;
    progress.total_points = g_point_count;
    progress.link_ready = (g_fpga_device != nullptr);
    progress.caps_received = g_caps_response_seen;
    progress.snapshot_started = g_defs_request_sent || (g_defs_loaded_bytes != 0u) || g_defs_response_seen ||
                              g_states_request_sent || (g_snapshot_record_count != 0u) || g_states_response_seen;
    progress.snapshot_complete = g_defs_response_seen && g_states_response_seen;

    if (!progress.link_ready) {
        return progress;
    }

    if (!progress.caps_received) {
        progress.percent = kBootPercentLinkReady;
        return progress;
    }

    progress.percent = compute_snapshot_percent();
    return progress;
}

SnapshotInfo get_snapshot_info()
{
    SnapshotInfo info = {};
    info.point_count = g_point_count;
    info.loaded_points = g_snapshot_record_count;
    info.max_payload = g_max_fragment_payload;
    info.complete = g_states_response_seen;
    info.sequence = g_states_sequence;
    if (state_try_lock(kStateReadLockTimeoutTicks)) {
        info.loaded_points = static_cast<uint16_t>(std::min<size_t>(g_snapshot_record_count, g_state_records.size()));
        state_unlock();
    }
    return info;
}

DefinitionsInfo get_definitions_info()
{
    DefinitionsInfo info = {};
    info.point_count = g_point_count;
    info.complete = g_defs_response_seen;
    info.generation = g_defs_generation;
    info.loaded_bytes = g_defs_loaded_bytes;
    info.total_bytes = g_defs_total_bytes;
    return info;
}

size_t copy_state_records(plclink::StateRecordV1* out_records, size_t max_records)
{
    if ((out_records == nullptr) || (max_records == 0u)) {
        return 0u;
    }

    state_lock();
    const size_t copy_count = std::min(max_records, g_state_records.size());
    if (copy_count != 0u) {
        for (size_t index = 0u; index < copy_count; ++index) {
            out_records[index] = g_state_records[index].record;
        }
    }
    state_unlock();
    return copy_count;
}

size_t copy_cached_state_records(CachedStateRecord* out_records, size_t max_records)
{
    if ((out_records == nullptr) || (max_records == 0u)) {
        return 0u;
    }

    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return 0u;
    }
    const size_t copy_count = std::min(max_records, g_state_records.size());
    if (copy_count != 0u) {
        std::memcpy(out_records, g_state_records.data(), copy_count * sizeof(CachedStateRecord));
    }
    state_unlock();
    return copy_count;
}

size_t copy_definition_records(plclink::DefinitionRecordV1* out_records, size_t max_records)
{
    if ((out_records == nullptr) || (max_records == 0u)) {
        return 0u;
    }

    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return 0u;
    }
    const size_t copy_count = std::min(max_records, g_definition_records.size());
    if (copy_count != 0u) {
        std::memcpy(out_records, g_definition_records.data(), copy_count * sizeof(plclink::DefinitionRecordV1));
    }
    state_unlock();
    return copy_count;
}

bool copy_cached_state_record(size_t index, CachedStateRecord* out_record)
{
    if (out_record == nullptr) {
        return false;
    }

    bool copied = false;
    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return false;
    }
    if (index < g_state_records.size()) {
        *out_record = g_state_records[index];
        copied = true;
    }
    state_unlock();
    return copied;
}

bool copy_definition_record(size_t index, plclink::DefinitionRecordV1* out_record)
{
    if (out_record == nullptr) {
        return false;
    }

    bool copied = false;
    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return false;
    }
    if (index < g_definition_records.size()) {
        *out_record = g_definition_records[index];
        copied = true;
    }
    state_unlock();
    return copied;
}

bool pop_point_update(PointUpdate* out_update)
{
    if (out_update == nullptr) {
        return false;
    }

    bool copied = false;
    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return false;
    }
    if (g_pending_point_update_count != 0u) {
        const PendingPointUpdate pending = g_pending_point_updates[g_pending_point_update_head];
        g_pending_point_update_head = (g_pending_point_update_head + 1u) % kMaxPendingPointUpdates;
        --g_pending_point_update_count;

        const size_t index = static_cast<size_t>(pending.point_index);
        if (index < g_state_records.size()) {
            *out_update = {};
            out_update->sequence = pending.sequence;
            out_update->point_count = g_point_count;
            out_update->loaded_points = static_cast<uint16_t>(std::min<size_t>(g_snapshot_record_count, g_state_records.size()));
            out_update->complete = g_states_response_seen;
            out_update->state = g_state_records[index];
            if (index < g_definition_records.size()) {
                out_update->definition = g_definition_records[index];
                out_update->has_definition = true;
            }
            copied = true;
        }
    }
    state_unlock();
    return copied;
}

bool copy_string_state_by_path(const char* feature, const char* point_id, char* out_value, size_t out_size)
{
    if ((feature == nullptr) || (point_id == nullptr) || (out_value == nullptr) || (out_size == 0u)) {
        return false;
    }

    out_value[0] = '\0';

    bool found = false;
    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return false;
    }
    const CachedStateRecord* state_record = nullptr;
    if (find_state_record_by_path_locked(feature, point_id, kPointValueTypeString, &state_record)) {
        std::strncpy(out_value, state_record->string_value, out_size - 1u);
        out_value[out_size - 1u] = '\0';
        found = true;
    }
    state_unlock();
    return found;
}

bool copy_bool_state_by_path(const char* feature, const char* point_id, bool* out_value)
{
    if ((feature == nullptr) || (point_id == nullptr) || (out_value == nullptr)) {
        return false;
    }

    bool found = false;
    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return false;
    }
    const CachedStateRecord* state_record = nullptr;
    if (find_state_record_by_path_locked(feature, point_id, kPointValueTypeBool, &state_record)) {
        *out_value = (state_record->record.value_bits & 0x1u) != 0u;
        found = true;
    }
    state_unlock();
    return found;
}

bool copy_u32_state_by_path(const char* feature, const char* point_id, uint32_t* out_value)
{
    if ((feature == nullptr) || (point_id == nullptr) || (out_value == nullptr)) {
        return false;
    }

    bool found = false;
    if (!state_try_lock(kStateReadLockTimeoutTicks)) {
        return false;
    }
    const CachedStateRecord* state_record = nullptr;
    if (find_state_record_by_path_locked(feature, point_id, kPointValueTypeUint32, &state_record)) {
        *out_value = state_record->record.value_bits;
        found = true;
    }
    state_unlock();
    return found;
}

esp_err_t request_states_refresh()
{
    if (!g_caps_response_seen) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_states_request_sent || !g_states_response_seen) {
        return ESP_ERR_INVALID_STATE;
    }

    g_refresh_requested = true;
    return ESP_OK;
}

esp_err_t request_write_state(uint16_t point_index,
                              uint8_t expected_value_type,
                              uint8_t write_flags,
                              uint32_t value_bits)
{
    if (!g_caps_response_seen || !g_states_response_seen) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    state_lock();
    if (g_pending_write_state.valid || g_write_state_request_sent) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        g_pending_write_state.valid = true;
        g_pending_write_state.point_index = point_index;
        g_pending_write_state.expected_value_type = expected_value_type;
        g_pending_write_state.write_flags = write_flags;
        g_pending_write_state.value_bits = value_bits;
    }
    state_unlock();
    return result;
}

} // namespace spi_link