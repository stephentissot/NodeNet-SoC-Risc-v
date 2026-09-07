#include "spi_link.h"

#include <cstdio>
#include <cstring>

#include <esp_check.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>

#include "app_config.h"
#include "protocol_codec.h"
#include "protocol_states.h"
#include "protocol_types.h"
#include "spi_bus_shared.h"

namespace spi_link {
namespace {

constexpr const char* kLogTag = "spi-link";
constexpr uint8_t kOpcodeReadStatus = 0x01;
constexpr uint8_t kOpcodeWriteRequest = 0x02;
constexpr uint8_t kOpcodeReadResponse = 0x03;
constexpr uint8_t kOpcodeWriteControl = 0x04;
constexpr uint16_t kControlClearIrq = 1u << 2;
constexpr size_t kMaxMailboxPayload = 128;
constexpr uint8_t kBootPercentLinkReady = 25u;
constexpr uint8_t kBootPercentCapsReady = 45u;
constexpr uint8_t kBootPercentSnapshotBase = 45u;
constexpr uint8_t kBootPercentSnapshotSpan = 54u;
DMA_ATTR uint8_t g_mailbox_tx_buffer[3 + kMaxMailboxPayload] = {};
DMA_ATTR uint8_t g_mailbox_rx_buffer[3 + kMaxMailboxPayload] = {};
spi_device_handle_t g_fpga_device = nullptr;
int g_last_irq_level = -1;
uint32_t g_poll_count = 0;
uint32_t g_frame_count = 0;
uint16_t g_last_status = 0xFFFFu;
bool g_caps_request_sent = false;
bool g_caps_response_seen = false;
bool g_states_request_sent = false;
bool g_states_response_seen = false;
uint16_t g_next_states_start_index = 0u;
uint8_t g_next_request_id = 1u;
uint16_t g_point_count = 0u;
uint16_t g_max_fragment_payload = 0u;
uint16_t g_snapshot_chunk_count = 0u;
uint16_t g_snapshot_record_count = 0u;

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
    return g_caps_response_seen && !g_states_response_seen;
}

uint8_t compute_snapshot_percent()
{
    if (!g_caps_response_seen) {
        return kBootPercentLinkReady;
    }

    if (g_states_response_seen) {
        return 100u;
    }

    if ((g_point_count == 0u) || ((!g_states_request_sent) && (g_snapshot_record_count == 0u))) {
        return kBootPercentCapsReady;
    }

    const uint32_t completed = static_cast<uint32_t>(g_snapshot_record_count);
    const uint32_t total = (g_point_count == 0u) ? 1u : static_cast<uint32_t>(g_point_count);
    const uint32_t scaled = (completed * kBootPercentSnapshotSpan) / total;
    const uint32_t percent = static_cast<uint32_t>(kBootPercentSnapshotBase) + scaled;
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

esp_err_t drain_stale_response()
{
    uint8_t payload[kMaxMailboxPayload + 1] = {};
    uint16_t payload_len = 0u;
    ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                        kLogTag,
                        "stale response read failed");
    ESP_RETURN_ON_ERROR(write_control(kControlClearIrq),
                        kLogTag,
                        "stale clear_irq failed");

    ESP_LOGW(kLogTag,
             "Discarded stale mailbox response len=%u before plcLink request sequence",
             static_cast<unsigned>(payload_len));
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
    request.include_strings = 0u;
    std::memcpy(&request_buffer[plclink::kHeaderSize], &request, sizeof(request));

    ESP_RETURN_ON_ERROR(write_request(request_buffer, sizeof(request_buffer)),
                        kLogTag,
                        "write states snapshot request failed");
    g_states_request_sent = true;
    if (start_index == 0u) {
        g_snapshot_chunk_count = 0u;
        g_snapshot_record_count = 0u;
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

    gpio_config_t irq_config = {};
    irq_config.pin_bit_mask = (1ULL << static_cast<uint32_t>(app_config::kFpgaIrq));
    irq_config.mode = GPIO_MODE_INPUT;
    irq_config.pull_up_en = GPIO_PULLUP_DISABLE;
    irq_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irq_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&irq_config));

    ESP_RETURN_ON_ERROR(spi_bus_shared::ensure_bus(),
                        kLogTag,
                        "shared spi_bus_initialize failed");

    ESP_RETURN_ON_ERROR(add_device(app_config::kFpgaCs, &g_fpga_device),
                        kLogTag,
                        "spi_bus_add_device(fpga) failed");

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
    if ((irq_level != g_last_irq_level) && !is_snapshot_in_progress()) {
        ESP_LOGI(kLogTag,
                 "IRQ level changed to %d after %lu polls",
                 irq_level,
                 static_cast<unsigned long>(g_poll_count));
    }
    g_last_irq_level = irq_level;

    ESP_RETURN_ON_ERROR(read_status(&status, status_rx),
                        kLogTag,
                        "read_status failed");

    if (((status != g_last_status) || (g_poll_count == 1u)) && !is_snapshot_in_progress()) {
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

    if (!g_caps_request_sent && (status_bit(status, 5) != 0u)) {
        return drain_stale_response();
    }

    if (g_caps_response_seen && !g_states_request_sent && !g_states_response_seen && (status_bit(status, 5) != 0u)) {
        return drain_stale_response();
    }

    if (!g_caps_request_sent && status_bit(status, 3) != 0u) {
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
        ESP_RETURN_ON_ERROR(write_control(kControlClearIrq),
                            kLogTag,
                            "clear_irq failed");

        if (payload_len < plclink::kHeaderSize) {
            ESP_LOGE(kLogTag, "plcLink response too short len=%u", static_cast<unsigned>(payload_len));
            return ESP_ERR_INVALID_RESPONSE;
        }

        plclink::ProtocolHeader header = {};
        if (!plclink::decodeHeader(header, payload, payload_len)) {
            ESP_LOGE(kLogTag, "plcLink response header invalid");
            return ESP_ERR_INVALID_RESPONSE;
        }

        ++g_frame_count;
        g_caps_response_seen = true;

        if (header.message_type == plclink::kMsgGetCapsRes) {
            if (payload_len < (plclink::kHeaderSize + sizeof(plclink::CapsResponsePayload))) {
                ESP_LOGE(kLogTag,
                         "plcLink caps response payload too short len=%u",
                         static_cast<unsigned>(payload_len));
                return ESP_ERR_INVALID_RESPONSE;
            }

            plclink::CapsResponsePayload caps = {};
            std::memcpy(&caps, &payload[plclink::kHeaderSize], sizeof(caps));
            g_point_count = caps.point_count;
            g_max_fragment_payload = caps.max_fragment_payload;
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
        } else {
            ESP_LOGW(kLogTag,
                     "Unexpected plcLink response #%lu type=0x%02x len=%u",
                     static_cast<unsigned long>(g_frame_count),
                     static_cast<unsigned>(header.message_type),
                     static_cast<unsigned>(payload_len));
        }
        return ESP_OK;
    }

    if (g_caps_response_seen && !g_states_request_sent && !g_states_response_seen && (status_bit(status, 3) != 0u)) {
        return send_states_snapshot_request(g_next_states_start_index);
    }

    if (g_states_request_sent && !g_states_response_seen && (status_bit(status, 5) != 0u)) {
        uint8_t payload[kMaxMailboxPayload + 1] = {};
        uint16_t payload_len = 0;
        ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                            kLogTag,
                            "read states response failed");
        ESP_RETURN_ON_ERROR(write_control(kControlClearIrq),
                            kLogTag,
                            "clear_irq failed");

        if (payload_len < (plclink::kHeaderSize + sizeof(plclink::StatesSnapshotChunkPrefix))) {
            ESP_LOGE(kLogTag, "plcLink states response too short len=%u", static_cast<unsigned>(payload_len));
            return ESP_ERR_INVALID_RESPONSE;
        }

        plclink::ProtocolHeader header = {};
        if (!plclink::decodeHeader(header, payload, payload_len)) {
            ESP_LOGE(kLogTag, "plcLink states response header invalid");
            return ESP_ERR_INVALID_RESPONSE;
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
        const size_t expected_payload = sizeof(prefix) +
                                        (static_cast<size_t>(prefix.returned_record_count) * sizeof(plclink::StateRecordV1));
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
           (!g_caps_response_seen || !g_states_response_seen || g_caps_request_sent || g_states_request_sent);
}

BootProgress get_boot_progress()
{
    BootProgress progress = {};
    progress.percent = 10u;
    progress.loaded_points = g_snapshot_record_count;
    progress.total_points = g_point_count;
    progress.link_ready = (g_fpga_device != nullptr);
    progress.caps_received = g_caps_response_seen;
    progress.snapshot_started = g_states_request_sent || (g_snapshot_record_count != 0u) || g_states_response_seen;
    progress.snapshot_complete = g_states_response_seen;

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

} // namespace spi_link