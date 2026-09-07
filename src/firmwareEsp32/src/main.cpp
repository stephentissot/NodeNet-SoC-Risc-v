#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_log.h>

#include "app_config.h"
#include "display_st7789.h"
#include "spi_link.h"

namespace {

constexpr const char* kLogTag = "esp32-main";
constexpr unsigned long kPollIntervalIdleMs = 20;
constexpr unsigned long kPollIntervalBusyMs = 1;
TickType_t g_last_poll_tick = 0;

} // namespace

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(kLogTag, "ESP32 firmware start");

    if (!app_config::kDisplayBringupOwnsSpiBus) {
        const esp_err_t init_result = spi_link::init();
        if (init_result != ESP_OK) {
            ESP_LOGE(kLogTag, "spi_link::init failed: %s", esp_err_to_name(init_result));
            return;
        }
    }

    const esp_err_t display_result = display_st7789::init();
    if (display_result != ESP_OK) {
        ESP_LOGE(kLogTag, "display_st7789::init failed: %s", esp_err_to_name(display_result));
    } else {
        ESP_LOGI(kLogTag, "display_st7789::init OK");
    }

    if (app_config::kDisplayBringupOwnsSpiBus) {
        ESP_LOGI(kLogTag, "Display bring-up owns SPI bus; FPGA SPI link temporarily disabled");
    }

    for (;;) {
        if (app_config::kDisplayBringupOwnsSpiBus) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        const TickType_t poll_interval = pdMS_TO_TICKS(
            spi_link::is_busy() ? kPollIntervalBusyMs : kPollIntervalIdleMs);
        if ((now - g_last_poll_tick) < poll_interval) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        g_last_poll_tick = now;

        const esp_err_t poll_result = spi_link::poll();
        if (poll_result != ESP_OK) {
            ESP_LOGE(kLogTag, "spi_link::poll failed: %s", esp_err_to_name(poll_result));
        }
    }
}
