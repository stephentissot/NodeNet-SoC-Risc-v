#include "lvgl_port.h"

#include <cstdint>

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "app_config.h"
#include "display_st7789.h"
#include "lvgl.h"

namespace lvgl_port {
namespace {

constexpr const char* kLogTag = "lvgl-port";
constexpr uint32_t kLvglTickMs = 5;
constexpr size_t kBufferRows = 24;
constexpr size_t kBytesPerPixel = 2;

lv_display_t* g_display = nullptr;
esp_timer_handle_t g_tick_timer = nullptr;
LV_ATTRIBUTE_MEM_ALIGN static uint8_t g_draw_buf_1[app_config::kDisplayWidth * kBufferRows * kBytesPerPixel] = {};
LV_ATTRIBUTE_MEM_ALIGN static uint8_t g_draw_buf_2[app_config::kDisplayWidth * kBufferRows * kBytesPerPixel] = {};

void lvgl_tick_callback(void*)
{
    lv_tick_inc(kLvglTickMs);
}

void flush_callback(lv_display_t* display, const lv_area_t* area, uint8_t* px_map)
{
    const uint16_t width = static_cast<uint16_t>(area->x2 - area->x1 + 1);
    const uint16_t height = static_cast<uint16_t>(area->y2 - area->y1 + 1);
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);

    const esp_err_t result = display_st7789::blit_rgb565(static_cast<uint16_t>(area->x1),
                                                         static_cast<uint16_t>(area->y1),
                                                         width,
                                                         height,
                                                         reinterpret_cast<const uint16_t*>(px_map),
                                                         pixel_count);
    if (result != ESP_OK) {
        ESP_LOGE(kLogTag, "blit_rgb565 failed: %s", esp_err_to_name(result));
    }

    lv_display_flush_ready(display);
}

} // namespace

esp_err_t init()
{
    if (g_display != nullptr) {
        return ESP_OK;
    }

    lv_init();

    const esp_timer_create_args_t timer_args = {
        .callback = &lvgl_tick_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = false,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &g_tick_timer), kLogTag, "esp_timer_create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(g_tick_timer, kLvglTickMs * 1000),
                        kLogTag,
                        "esp_timer_start_periodic failed");

    g_display = lv_display_create(app_config::kDisplayWidth, app_config::kDisplayHeight);
    if (g_display == nullptr) {
        return ESP_FAIL;
    }

    lv_display_set_flush_cb(g_display, flush_callback);
    lv_display_set_buffers(g_display,
                           g_draw_buf_1,
                           g_draw_buf_2,
                           sizeof(g_draw_buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    return ESP_OK;
}

void task_handler()
{
    lv_timer_handler();
}

} // namespace lvgl_port
