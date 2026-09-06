#include "spi_link.h"

#include <cstring>

#include <esp_check.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>

#include "app_config.h"
#include "spi_bus_shared.h"

namespace spi_link {
namespace {

constexpr const char* kLogTag = "spi-link";
constexpr uint8_t kOpcodeReadStatus = 0x01;
constexpr uint8_t kOpcodeWriteRequest = 0x02;
constexpr uint8_t kOpcodeReadResponse = 0x03;
constexpr uint8_t kOpcodeWriteControl = 0x04;
constexpr uint16_t kControlClearIrq = 1u << 2;
constexpr size_t kMaxMailboxPayload = 64;
constexpr char kHelloWorldExpected[] = "Hello World";
spi_device_handle_t g_fpga_device = nullptr;
int g_last_irq_level = -1;
uint32_t g_poll_count = 0;
uint32_t g_frame_count = 0;
uint16_t g_last_status = 0xFFFFu;
bool g_hello_request_sent = false;
bool g_hello_response_seen = false;

constexpr uint16_t status_bit(uint16_t status, uint8_t bit)
{
    return static_cast<uint16_t>((status >> bit) & 0x1u);
}

esp_err_t read_status(uint16_t* out_status, uint8_t* out_rx)
{
    if ((g_fpga_device == nullptr) || (out_status == nullptr) || (out_rx == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t tx_buffer[3] = {kOpcodeReadStatus, 0x00, 0x00};
    spi_transaction_t transaction = {};
    transaction.length = sizeof(tx_buffer) * 8;
    transaction.tx_buffer = tx_buffer;
    transaction.rx_buffer = out_rx;

    ESP_RETURN_ON_ERROR(spi_device_transmit(g_fpga_device, &transaction),
                        kLogTag,
                        "status spi_device_transmit failed");

    *out_status = static_cast<uint16_t>(out_rx[1]) |
                  (static_cast<uint16_t>(out_rx[2]) << 8);
    return ESP_OK;
}

esp_err_t write_request(const uint8_t* payload, uint16_t payload_len)
{
    if ((g_fpga_device == nullptr) || ((payload_len != 0u) && (payload == nullptr))) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buffer[3 + kMaxMailboxPayload] = {};
    if (payload_len > kMaxMailboxPayload) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx_buffer[0] = kOpcodeWriteRequest;
    tx_buffer[1] = static_cast<uint8_t>(payload_len & 0xFFu);
    tx_buffer[2] = static_cast<uint8_t>((payload_len >> 8) & 0xFFu);
    if (payload_len != 0u) {
        std::memcpy(&tx_buffer[3], payload, payload_len);
    }

    spi_transaction_t transaction = {};
    transaction.length = static_cast<size_t>(3u + payload_len) * 8u;
    transaction.tx_buffer = tx_buffer;

    return spi_device_transmit(g_fpga_device, &transaction);
}

esp_err_t write_control(uint16_t control_word)
{
    if (g_fpga_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t tx_buffer[3] = {
        kOpcodeWriteControl,
        static_cast<uint8_t>(control_word & 0xFFu),
        static_cast<uint8_t>((control_word >> 8) & 0xFFu),
    };

    spi_transaction_t transaction = {};
    transaction.length = sizeof(tx_buffer) * 8u;
    transaction.tx_buffer = tx_buffer;

    return spi_device_transmit(g_fpga_device, &transaction);
}

esp_err_t read_response(uint8_t* out_payload, uint16_t capacity, uint16_t* out_payload_len)
{
    if ((g_fpga_device == nullptr) || (out_payload_len == nullptr) || ((capacity != 0u) && (out_payload == nullptr))) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t length_tx[3] = {kOpcodeReadResponse, 0x00, 0x00};
    uint8_t length_rx[3] = {};
    spi_transaction_t length_transaction = {};
    length_transaction.length = sizeof(length_tx) * 8u;
    length_transaction.tx_buffer = length_tx;
    length_transaction.rx_buffer = length_rx;
    ESP_RETURN_ON_ERROR(spi_device_transmit(g_fpga_device, &length_transaction),
                        kLogTag,
                        "response length read failed");

    const uint16_t payload_len = static_cast<uint16_t>(length_rx[1]) |
                                 (static_cast<uint16_t>(length_rx[2]) << 8);
    *out_payload_len = payload_len;
    if (payload_len > capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (payload_len == 0u) {
        return ESP_OK;
    }

    uint8_t tx_buffer[3 + kMaxMailboxPayload] = {};
    uint8_t rx_buffer[3 + kMaxMailboxPayload] = {};
    tx_buffer[0] = kOpcodeReadResponse;

    spi_transaction_t payload_transaction = {};
    payload_transaction.length = static_cast<size_t>(3u + payload_len) * 8u;
    payload_transaction.tx_buffer = tx_buffer;
    payload_transaction.rx_buffer = rx_buffer;
    ESP_RETURN_ON_ERROR(spi_device_transmit(g_fpga_device, &payload_transaction),
                        kLogTag,
                        "response payload read failed");

    std::memcpy(out_payload, &rx_buffer[3], payload_len);
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
    if (irq_level != g_last_irq_level) {
        ESP_LOGI(kLogTag,
                 "IRQ level changed to %d after %lu polls",
                 irq_level,
                 static_cast<unsigned long>(g_poll_count));
        g_last_irq_level = irq_level;
    }

    ESP_RETURN_ON_ERROR(read_status(&status, status_rx),
                        kLogTag,
                        "read_status failed");

    if ((status != g_last_status) || (g_poll_count == 1u)) {
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
        g_last_status = status;
    }

    if (!g_hello_request_sent && status_bit(status, 3) != 0u) {
        static constexpr uint8_t kHelloProbe[] = {'p', 'i', 'n', 'g'};
        ESP_RETURN_ON_ERROR(write_request(kHelloProbe, sizeof(kHelloProbe)),
                            kLogTag,
                            "write_request failed");
        g_hello_request_sent = true;
        ESP_LOGI(kLogTag,
                 "TX hello probe len=%u payload=%c%c%c%c",
                 static_cast<unsigned>(sizeof(kHelloProbe)),
                 kHelloProbe[0],
                 kHelloProbe[1],
                 kHelloProbe[2],
                 kHelloProbe[3]);
        return ESP_OK;
    }

    if (g_hello_request_sent && !g_hello_response_seen && (status_bit(status, 5) != 0u)) {
        uint8_t payload[kMaxMailboxPayload + 1] = {};
        uint16_t payload_len = 0;
        ESP_RETURN_ON_ERROR(read_response(payload, kMaxMailboxPayload, &payload_len),
                            kLogTag,
                            "read_response failed");
        ESP_RETURN_ON_ERROR(write_control(kControlClearIrq),
                            kLogTag,
                            "clear_irq failed");

        payload[payload_len < kMaxMailboxPayload ? payload_len : kMaxMailboxPayload] = 0;
        ++g_frame_count;
        g_hello_response_seen = true;

        ESP_LOGI(kLogTag,
                 "RX hello response #%lu len=%u text='%s'",
                 static_cast<unsigned long>(g_frame_count),
                 static_cast<unsigned>(payload_len),
                 reinterpret_cast<const char*>(payload));

        if ((payload_len != (sizeof(kHelloWorldExpected) - 1u)) ||
            (std::memcmp(payload, kHelloWorldExpected, sizeof(kHelloWorldExpected) - 1u) != 0)) {
            ESP_LOGW(kLogTag,
                     "Unexpected hello response len=%u expected='%s'",
                     static_cast<unsigned>(payload_len),
                     kHelloWorldExpected);
        }
    }

    return ESP_OK;
}

} // namespace spi_link