#include "boot_ui.h"

#include <algorithm>
#include <cstdint>

#include <esp_random.h>

#include "app_config.h"
#include "boot_assets.h"
#include "lvgl.h"

namespace boot_ui {
namespace {

lv_obj_t* g_root = nullptr;
lv_obj_t* g_percent_label = nullptr;
lv_obj_t* g_status_label = nullptr;
lv_obj_t* g_progress_bar = nullptr;

const lv_image_dsc_t* pick_image()
{
    return (esp_random() & 1u) == 0u ? &boot_assets::g_boot_logo_hd : &boot_assets::g_boot_bigsister;
}

} // namespace

esp_err_t init()
{
    if (g_root != nullptr) {
        return ESP_OK;
    }

    constexpr lv_coord_t kScreenWidth = app_config::kDisplayWidth;
    constexpr lv_coord_t kScreenHeight = app_config::kDisplayHeight;
    constexpr lv_coord_t kImageSize = 40;
    constexpr lv_coord_t kOuterPad = 8;
    constexpr lv_coord_t kContentLeft = kOuterPad + kImageSize + 10;
    constexpr lv_coord_t kContentWidth = kScreenWidth - kContentLeft - kOuterPad;

    g_root = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x050816), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    lv_obj_t* image = lv_image_create(g_root);
    lv_image_set_src(image, pick_image());
    lv_obj_set_pos(image, kOuterPad, (kScreenHeight - kImageSize) / 2);

    lv_obj_t* title = lv_label_create(g_root);
    lv_label_set_text(title, "BOOT");
    lv_obj_set_style_text_color(title, lv_color_hex(0xDCE3FF), 0);
    lv_obj_set_pos(title, kContentLeft, 8);

    g_percent_label = lv_label_create(g_root);
    lv_label_set_text(g_percent_label, "0%");
    lv_obj_set_style_text_color(g_percent_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(g_percent_label, title, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    g_progress_bar = lv_bar_create(g_root);
    lv_obj_set_size(g_progress_bar, kContentWidth, 14);
    lv_obj_set_pos(g_progress_bar, kContentLeft, 30);
    lv_bar_set_range(g_progress_bar, 0, 100);
    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(g_progress_bar, 7, 0);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x182136), 0);
    lv_obj_set_style_bg_opa(g_progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_progress_bar, 0, 0);
    lv_obj_set_style_pad_all(g_progress_bar, 2, 0);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x2F68FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(g_progress_bar, lv_color_hex(0x8ED0FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(g_progress_bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);

    g_status_label = lv_label_create(g_root);
    lv_obj_set_width(g_status_label, kContentWidth);
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xAEB9D8), 0);
    lv_label_set_text(g_status_label, "Initialisation");
    lv_obj_set_pos(g_status_label, kContentLeft, 50);

    lv_screen_load(g_root);
    return ESP_OK;
}

void set_progress(uint8_t percent, const char* status_text)
{
    if ((g_root == nullptr) || (g_percent_label == nullptr) || (g_status_label == nullptr) || (g_progress_bar == nullptr)) {
        return;
    }

    const uint8_t clamped = static_cast<uint8_t>(std::min<int>(percent, 100));
    lv_label_set_text_fmt(g_percent_label, "%u%%", static_cast<unsigned>(clamped));
    lv_bar_set_value(g_progress_bar, clamped, LV_ANIM_ON);
    lv_label_set_text(g_status_label, (status_text != nullptr) ? status_text : "");
}

} // namespace boot_ui
