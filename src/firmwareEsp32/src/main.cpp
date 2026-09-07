#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#include <esp_err.h>
#include <esp_log.h>

#include "app_config.h"
#include "boot_ui.h"
#include "display_st7789.h"
#include "lvgl_port.h"
#include "spi_link.h"

namespace {

constexpr const char* kLogTag = "esp32-main";
constexpr unsigned long kPollIntervalIdleMs = 20;
constexpr unsigned long kPollIntervalBusyMs = 1;
TickType_t g_last_poll_tick = 0;
uint8_t g_last_boot_percent = 0xFFu;
char g_last_boot_text[64] = {};

uint8_t quantize_boot_percent(uint8_t percent)
{
    if (percent >= 100u) {
        return 100u;
    }
    return static_cast<uint8_t>((percent / 5u) * 5u);
}

void update_boot_progress_ui(bool force)
{
    const spi_link::BootProgress progress = spi_link::get_boot_progress();
    const uint8_t display_percent = quantize_boot_percent(progress.percent);

    char status_text[64] = {};
    if (!progress.link_ready) {
        std::snprintf(status_text, sizeof(status_text), "Init SPI");
    } else if (!progress.caps_received) {
        std::snprintf(status_text, sizeof(status_text), "Lecture capacites FPGA");
    } else if (!progress.snapshot_started) {
        std::snprintf(status_text, sizeof(status_text), "Preparation snapshot");
    } else if (!progress.snapshot_complete) {
        std::snprintf(status_text, sizeof(status_text), "Chargement points");
    } else {
        std::snprintf(status_text, sizeof(status_text), "Demarrage OK");
    }

    if (!force && (display_percent == g_last_boot_percent) && (std::strcmp(status_text, g_last_boot_text) == 0)) {
        return;
    }

    boot_ui::set_progress(display_percent, status_text);
    lvgl_port::task_handler();
    g_last_boot_percent = display_percent;
    std::snprintf(g_last_boot_text, sizeof(g_last_boot_text), "%s", status_text);
}

} // namespace

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(kLogTag, "ESP32 firmware start");

    const esp_err_t display_result = display_st7789::init();
    if (display_result != ESP_OK) {
        ESP_LOGE(kLogTag, "display_st7789::init failed: %s", esp_err_to_name(display_result));
        return;
    } else {
        ESP_LOGI(kLogTag, "display_st7789::init OK");
    }

    const esp_err_t lvgl_result = lvgl_port::init();
    if (lvgl_result != ESP_OK) {
        ESP_LOGE(kLogTag, "lvgl_port::init failed: %s", esp_err_to_name(lvgl_result));
        return;
    }

    const esp_err_t boot_ui_result = boot_ui::init();
    if (boot_ui_result != ESP_OK) {
        ESP_LOGE(kLogTag, "boot_ui::init failed: %s", esp_err_to_name(boot_ui_result));
        return;
    }

    boot_ui::set_progress(10, "Ecran pret");
    lvgl_port::task_handler();

    if (app_config::kDisplayBringupOwnsSpiBus) {
        ESP_LOGI(kLogTag, "Display bring-up owns SPI bus; FPGA SPI link temporarily disabled");
        boot_ui::set_progress(100, "Mode ecran seul");
        lvgl_port::task_handler();
    } else {
        boot_ui::set_progress(35, "Lien FPGA");
        lvgl_port::task_handler();

        const esp_err_t init_result = spi_link::init();
        if (init_result != ESP_OK) {
            boot_ui::set_progress(100, "Erreur SPI");
            lvgl_port::task_handler();
            ESP_LOGE(kLogTag, "spi_link::init failed: %s", esp_err_to_name(init_result));
            return;
        }

        update_boot_progress_ui(true);
    }

    for (;;) {
        lvgl_port::task_handler();

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
            boot_ui::set_progress(100, "Erreur SPI");
            lvgl_port::task_handler();
            ESP_LOGE(kLogTag, "spi_link::poll failed: %s", esp_err_to_name(poll_result));
        } else {
            update_boot_progress_ui(false);
        }
    }
}
