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
#include "status_ui.h"
#include "ui_text.h"
#include "web_server.h"
#include "wifi_manager.h"

namespace {

constexpr const char* kLogTag = "esp32-main";
constexpr unsigned long kPollIntervalIdleMs = 20;
constexpr unsigned long kPollIntervalBusyMs = 1;
TickType_t g_last_poll_tick = 0;
uint8_t g_last_boot_percent = 0xFFu;
char g_last_boot_text[64] = {};
bool g_runtime_screen_shown = false;

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
    const wifi_manager::SiteSettings settings = wifi_manager::get_site_settings();
    const char* language = settings.language;

    char status_text[64] = {};
    if (!progress.link_ready) {
        std::snprintf(status_text, sizeof(status_text), "%s", ui_text::boot_stage_text(language, ui_text::BootStage::InitSpi));
    } else if (!progress.caps_received) {
        std::snprintf(status_text, sizeof(status_text), "%s", ui_text::boot_stage_text(language, ui_text::BootStage::ReadFpgaCaps));
    } else if (!progress.snapshot_started) {
        std::snprintf(status_text, sizeof(status_text), "%s", ui_text::boot_stage_text(language, ui_text::BootStage::PrepareSnapshot));
    } else if (!progress.snapshot_complete) {
        std::snprintf(status_text, sizeof(status_text), "%s", ui_text::boot_stage_text(language, ui_text::BootStage::LoadingPoints));
    } else {
        std::snprintf(status_text, sizeof(status_text), "%s", ui_text::boot_stage_text(language, ui_text::BootStage::StartupOk));
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
    bool spi_ready = false;

    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(kLogTag, "ESP32 firmware start");

    const esp_err_t preload_result = wifi_manager::preload_site_settings();
    if (preload_result != ESP_OK) {
        ESP_LOGW(kLogTag, "wifi_manager::preload_site_settings failed: %s", esp_err_to_name(preload_result));
    }

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

    const esp_err_t status_ui_result = status_ui::init();
    if (status_ui_result != ESP_OK) {
        ESP_LOGE(kLogTag, "status_ui::init failed: %s", esp_err_to_name(status_ui_result));
        return;
    }

    const wifi_manager::SiteSettings initial_settings = wifi_manager::get_site_settings();

    boot_ui::set_progress(10, ui_text::boot_stage_text(initial_settings.language, ui_text::BootStage::ScreenReady));
    lvgl_port::task_handler();

    boot_ui::set_progress(18, ui_text::boot_stage_text(initial_settings.language, ui_text::BootStage::WifiWeb));
    lvgl_port::task_handler();

    const esp_err_t wifi_result = wifi_manager::init();
    if (wifi_result != ESP_OK) {
        ESP_LOGE(kLogTag, "wifi_manager::init failed: %s", esp_err_to_name(wifi_result));
    } else {
        const esp_err_t web_result = web_server::start();
        if (web_result != ESP_OK) {
            ESP_LOGE(kLogTag, "web_server::start failed: %s", esp_err_to_name(web_result));
        }
    }

    if (app_config::kDisplayBringupOwnsSpiBus) {
        ESP_LOGI(kLogTag, "Display bring-up owns SPI bus; FPGA SPI link temporarily disabled");
        boot_ui::set_progress(100, ui_text::boot_stage_text(initial_settings.language, ui_text::BootStage::DisplayOnly));
        lvgl_port::task_handler();
    } else {
        boot_ui::set_progress(35, ui_text::boot_stage_text(initial_settings.language, ui_text::BootStage::FpgaLink));
        lvgl_port::task_handler();

        const esp_err_t init_result = spi_link::init();
        if (init_result != ESP_OK) {
            boot_ui::set_progress(100, ui_text::boot_stage_text(initial_settings.language, ui_text::BootStage::SpiError));
            lvgl_port::task_handler();
            ESP_LOGE(kLogTag, "spi_link::init failed: %s", esp_err_to_name(init_result));
        } else {
            spi_ready = true;
            update_boot_progress_ui(true);
        }
    }

    for (;;) {
        const wifi_manager::Status wifi = wifi_manager::get_status();
        const wifi_manager::SiteSettings settings = wifi_manager::get_site_settings();
        status_ui::update(settings.language, wifi);

        lvgl_port::task_handler();
        web_server::poll();

        if (app_config::kDisplayBringupOwnsSpiBus || !spi_ready) {
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
            boot_ui::set_progress(100, ui_text::boot_stage_text(settings.language, ui_text::BootStage::SpiError));
            lvgl_port::task_handler();
            ESP_LOGE(kLogTag, "spi_link::poll failed: %s", esp_err_to_name(poll_result));
        } else {
            const spi_link::BootProgress progress = spi_link::get_boot_progress();
            if (progress.snapshot_complete && !g_runtime_screen_shown) {
                status_ui::show();
                g_runtime_screen_shown = true;
            }

            if (!g_runtime_screen_shown) {
                update_boot_progress_ui(false);
            }
        }
    }
}
