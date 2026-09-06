#include "display_st7789.h"

#include <array>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"
#include "spi_bus_shared.h"

namespace display_st7789 {
namespace {

constexpr const char* kLogTag = "display-st7789";

constexpr uint8_t kCmdSwReset = 0x01;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdNormalOn = 0x13;
constexpr uint8_t kCmdInvOff = 0x20;
constexpr uint8_t kCmdDispOn = 0x29;
constexpr uint8_t kCmdCaseT = 0x2A;
constexpr uint8_t kCmdRaseT = 0x2B;
constexpr uint8_t kCmdRamWr = 0x2C;
constexpr uint8_t kCmdMadCtl = 0x36;
constexpr uint8_t kCmdColMod = 0x3A;
constexpr uint8_t kCmdRamCtrl = 0xB0;
constexpr uint8_t kCmdPorchCtrl = 0xB2;
constexpr uint8_t kCmdDisplayFunction = 0xB6;
constexpr uint8_t kCmdGateCtrl = 0xB7;
constexpr uint8_t kCmdVcomSet = 0xBB;
constexpr uint8_t kCmdLcmCtrl = 0xC0;
constexpr uint8_t kCmdVdvVrhEnable = 0xC2;
constexpr uint8_t kCmdVrhSet = 0xC3;
constexpr uint8_t kCmdVdvSet = 0xC4;
constexpr uint8_t kCmdFrameRateCtrl = 0xC6;
constexpr uint8_t kCmdPowerCtrl1 = 0xD0;
constexpr uint8_t kCmdPositiveGamma = 0xE0;
constexpr uint8_t kCmdNegativeGamma = 0xE1;

spi_device_handle_t g_display_device = nullptr;

esp_err_t reset_panel()
{
    gpio_config_t control_pins = {};
    control_pins.pin_bit_mask = (1ULL << static_cast<uint32_t>(app_config::kDisplayDc)) |
                                (1ULL << static_cast<uint32_t>(app_config::kDisplayReset));
    control_pins.mode = GPIO_MODE_OUTPUT;
    control_pins.pull_up_en = GPIO_PULLUP_DISABLE;
    control_pins.pull_down_en = GPIO_PULLDOWN_DISABLE;
    control_pins.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&control_pins), kLogTag, "gpio_config failed");

    gpio_set_level(app_config::kDisplayDc, 1);
    gpio_set_level(app_config::kDisplayReset, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(app_config::kDisplayReset, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(app_config::kDisplayReset, 1);
    vTaskDelay(pdMS_TO_TICKS(250));
    return ESP_OK;
}

esp_err_t ensure_bus_and_device()
{
    if (g_display_device != nullptr) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(spi_bus_shared::ensure_bus(), kLogTag, "shared bus init failed");

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = app_config::kDisplayClockHz;
    device_config.mode = 3;
    device_config.spics_io_num = app_config::kDisplayCs;
    device_config.queue_size = 2;

    return spi_bus_add_device(app_config::kSpiHost, &device_config, &g_display_device);
}

esp_err_t transmit_bytes(bool is_data, const uint8_t* data, size_t size)
{
    if ((g_display_device == nullptr) || (data == nullptr) || (size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(app_config::kDisplayDc, is_data ? 1 : 0);

    spi_transaction_t transaction = {};
    transaction.length = static_cast<uint32_t>(size * 8);
    transaction.tx_buffer = data;
    return spi_device_transmit(g_display_device, &transaction);
}

esp_err_t write_command(uint8_t command)
{
    return transmit_bytes(false, &command, 1);
}

esp_err_t write_data(const uint8_t* data, size_t size)
{
    return transmit_bytes(true, data, size);
}

esp_err_t write_data8(uint8_t value)
{
    return write_data(&value, 1);
}

esp_err_t set_address_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t x0 = static_cast<uint16_t>(x + app_config::kDisplayXGap);
    const uint16_t x1 = static_cast<uint16_t>(x0 + width - 1);
    const uint16_t y0 = static_cast<uint16_t>(y + app_config::kDisplayYGap);
    const uint16_t y1 = static_cast<uint16_t>(y0 + height - 1);

    const uint8_t caset[] = {
        static_cast<uint8_t>(x0 >> 8),
        static_cast<uint8_t>(x0 & 0xFFu),
        static_cast<uint8_t>(x1 >> 8),
        static_cast<uint8_t>(x1 & 0xFFu),
    };
    const uint8_t raset[] = {
        static_cast<uint8_t>(y0 >> 8),
        static_cast<uint8_t>(y0 & 0xFFu),
        static_cast<uint8_t>(y1 >> 8),
        static_cast<uint8_t>(y1 & 0xFFu),
    };

    ESP_RETURN_ON_ERROR(write_command(kCmdCaseT), kLogTag, "CASET failed");
    ESP_RETURN_ON_ERROR(write_data(caset, sizeof(caset)), kLogTag, "CASET data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdRaseT), kLogTag, "RASET failed");
    ESP_RETURN_ON_ERROR(write_data(raset, sizeof(raset)), kLogTag, "RASET data failed");
    return write_command(kCmdRamWr);
}

esp_err_t init_panel_registers()
{
    const uint8_t gamma_pos[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
    const uint8_t gamma_neg[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};

    ESP_RETURN_ON_ERROR(write_command(kCmdSwReset), kLogTag, "SWRESET failed");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(write_command(kCmdSleepOut), kLogTag, "SLPOUT failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(write_command(kCmdNormalOn), kLogTag, "NORON failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdMadCtl), kLogTag, "MADCTL failed");
    ESP_RETURN_ON_ERROR(write_data8(0x08), kLogTag, "MADCTL data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdDisplayFunction), kLogTag, "B6 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x0A), kLogTag, "B6 data0 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x82), kLogTag, "B6 data1 failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdRamCtrl), kLogTag, "RAMCTRL failed");
    ESP_RETURN_ON_ERROR(write_data8(0x00), kLogTag, "RAMCTRL data0 failed");
    ESP_RETURN_ON_ERROR(write_data8(0xE0), kLogTag, "RAMCTRL data1 failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdColMod), kLogTag, "COLMOD failed");
    ESP_RETURN_ON_ERROR(write_data8(0x55), kLogTag, "COLMOD data failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(write_command(kCmdPorchCtrl), kLogTag, "PORCTRL failed");
    ESP_RETURN_ON_ERROR(write_data8(0x0C), kLogTag, "PORCTRL data0 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x0C), kLogTag, "PORCTRL data1 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x00), kLogTag, "PORCTRL data2 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x33), kLogTag, "PORCTRL data3 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x33), kLogTag, "PORCTRL data4 failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdGateCtrl), kLogTag, "GCTRL failed");
    ESP_RETURN_ON_ERROR(write_data8(0x35), kLogTag, "GCTRL data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdVcomSet), kLogTag, "VCOMS failed");
    ESP_RETURN_ON_ERROR(write_data8(0x28), kLogTag, "VCOMS data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdLcmCtrl), kLogTag, "LCMCTRL failed");
    ESP_RETURN_ON_ERROR(write_data8(0x0C), kLogTag, "LCMCTRL data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdVdvVrhEnable), kLogTag, "VDVVRHEN failed");
    ESP_RETURN_ON_ERROR(write_data8(0x01), kLogTag, "VDVVRHEN data0 failed");
    ESP_RETURN_ON_ERROR(write_data8(0xFF), kLogTag, "VDVVRHEN data1 failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdVrhSet), kLogTag, "VRHS failed");
    ESP_RETURN_ON_ERROR(write_data8(0x10), kLogTag, "VRHS data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdVdvSet), kLogTag, "VDVSET failed");
    ESP_RETURN_ON_ERROR(write_data8(0x20), kLogTag, "VDVSET data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdFrameRateCtrl), kLogTag, "FRCTR2 failed");
    ESP_RETURN_ON_ERROR(write_data8(0x0F), kLogTag, "FRCTR2 data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdPowerCtrl1), kLogTag, "PWCTRL1 failed");
    ESP_RETURN_ON_ERROR(write_data8(0xA4), kLogTag, "PWCTRL1 data0 failed");
    ESP_RETURN_ON_ERROR(write_data8(0xA1), kLogTag, "PWCTRL1 data1 failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdPositiveGamma), kLogTag, "PVGAMCTRL failed");
    ESP_RETURN_ON_ERROR(write_data(gamma_pos, sizeof(gamma_pos)), kLogTag, "PVGAMCTRL data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdNegativeGamma), kLogTag, "NVGAMCTRL failed");
    ESP_RETURN_ON_ERROR(write_data(gamma_neg, sizeof(gamma_neg)), kLogTag, "NVGAMCTRL data failed");
    ESP_RETURN_ON_ERROR(write_command(kCmdInvOff), kLogTag, "INVOFF failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(write_command(kCmdDispOn), kLogTag, "DISPON failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t fill_solid(uint16_t color)
{
    // Keep transfers small on the non-DMA SPI path; larger buffers can be
    // rejected as invalid by spi_device_transmit() on ESP32.
    std::array<uint8_t, 32> chunk = {};
    for (size_t index = 0; index < chunk.size(); index += 2) {
        chunk[index] = static_cast<uint8_t>(color >> 8);
        chunk[index + 1] = static_cast<uint8_t>(color & 0xFFu);
    }

    ESP_RETURN_ON_ERROR(set_address_window(0, 0, app_config::kDisplayWidth, app_config::kDisplayHeight),
                        kLogTag,
                        "set_address_window failed");

    uint32_t pixels_remaining = static_cast<uint32_t>(app_config::kDisplayWidth) *
                                static_cast<uint32_t>(app_config::kDisplayHeight);
    while (pixels_remaining > 0) {
        const size_t pixels_per_chunk = chunk.size() / 2;
        const size_t pixels_this_chunk = (pixels_remaining > pixels_per_chunk) ? pixels_per_chunk : pixels_remaining;
        const size_t bytes_this_chunk = pixels_this_chunk * 2;
        ESP_RETURN_ON_ERROR(write_data(chunk.data(), bytes_this_chunk), kLogTag, "RAMWR data failed");
        pixels_remaining -= static_cast<uint32_t>(pixels_this_chunk);
    }

    return ESP_OK;
}

} // namespace

esp_err_t init()
{
    ESP_RETURN_ON_ERROR(reset_panel(), kLogTag, "panel reset failed");
    ESP_RETURN_ON_ERROR(ensure_bus_and_device(), kLogTag, "display SPI setup failed");

    ESP_LOGI(kLogTag,
             "ESP-IDF ST7789 bring-up pins sclk=%d mosi=%d cs=%d dc=%d rst=%d spi_hz=%d window=%dx%d offset=(%d,%d)",
             app_config::kSpiSck,
             app_config::kSpiMosi,
             app_config::kDisplayCs,
             app_config::kDisplayDc,
             app_config::kDisplayReset,
             app_config::kDisplayClockHz,
             app_config::kDisplayWidth,
             app_config::kDisplayHeight,
             app_config::kDisplayXGap,
             app_config::kDisplayYGap);

    ESP_RETURN_ON_ERROR(init_panel_registers(), kLogTag, "panel init failed");
    ESP_RETURN_ON_ERROR(fill_solid(0x0000), kLogTag, "fill black failed");
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_RETURN_ON_ERROR(fill_solid(0xF800), kLogTag, "fill red failed");
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_RETURN_ON_ERROR(fill_solid(0x07E0), kLogTag, "fill green failed");
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_RETURN_ON_ERROR(fill_solid(0x001F), kLogTag, "fill blue failed");
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_RETURN_ON_ERROR(fill_solid(0xFFFF), kLogTag, "fill white failed");

    ESP_LOGI(kLogTag, "ESP-IDF ST7789 bring-up complete");
    return ESP_OK;
}

} // namespace display_st7789