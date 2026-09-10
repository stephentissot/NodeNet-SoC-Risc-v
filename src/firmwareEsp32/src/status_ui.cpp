#include "status_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "boot_assets.h"
#include "lvgl.h"
#include "spi_link.h"
#include "ui_text.h"

namespace status_ui {
namespace {

constexpr uint8_t kSlotStart = 0u;
constexpr uint8_t kSlotCount = 16u;
constexpr uint8_t kSlotCols = 8u;
constexpr uint32_t kPlcSlotStatusRunning = 2u;
constexpr uint32_t kPlcSlotStatusFaulted = 0x80000000u;

struct SlotWidget {
    lv_obj_t* card = nullptr;
    lv_obj_t* slot_label = nullptr;
    lv_obj_t* state_label = nullptr;
    char state = '?';
    bool pulsing = false;
};

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_title_label = nullptr;
lv_obj_t* g_subtitle_label = nullptr;
lv_obj_t* g_note_label = nullptr;
lv_obj_t* g_footer_label = nullptr;
SlotWidget g_slots[kSlotCount] = {};
char g_language[16] = "uk";
char g_footer_text[96] = {};
bool g_have_live_slot_states = false;

void pulse_exec(void* obj, int32_t value)
{
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

char slot_hex_digit(uint8_t slot_id)
{
    return static_cast<char>((slot_id < 10u) ? ('0' + slot_id) : ('A' + (slot_id - 10u)));
}

void set_card_palette(lv_obj_t* card, lv_obj_t* state_label, char state)
{
    lv_color_t bg = lv_color_hex(0x1A243B);
    lv_color_t border = lv_color_hex(0x3B4A68);
    lv_color_t text = lv_color_hex(0xC9D3EE);

    switch (state) {
    case 'R':
        bg = lv_color_hex(0x0F6A46);
        border = lv_color_hex(0x49E19B);
        text = lv_color_hex(0xF5FFF9);
        break;
    case 'F':
        bg = lv_color_hex(0x6E1B2C);
        border = lv_color_hex(0xFF728E);
        text = lv_color_hex(0xFFF4F7);
        break;
    case 'S':
        bg = lv_color_hex(0x725215);
        border = lv_color_hex(0xF1C75B);
        text = lv_color_hex(0xFFF8E5);
        break;
    case 'L':
        bg = lv_color_hex(0x1C4275);
        border = lv_color_hex(0x7DB8FF);
        text = lv_color_hex(0xF6FBFF);
        break;
    case '-':
        bg = lv_color_hex(0x212734);
        border = lv_color_hex(0x51617B);
        text = lv_color_hex(0x9AA8C2);
        break;
    default:
        bg = lv_color_hex(0x2A2F3D);
        border = lv_color_hex(0x6A7284);
        text = lv_color_hex(0xE3E6EE);
        break;
    }

    lv_obj_set_style_bg_color(card, bg, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
    lv_obj_set_style_border_color(card, border, 0);
    lv_obj_set_style_text_color(state_label, text, 0);
}

void start_pulse(SlotWidget& slot)
{
    if (slot.pulsing || slot.card == nullptr) {
        return;
    }

    lv_anim_t animation = {};
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, slot.card);
    lv_anim_set_exec_cb(&animation, pulse_exec);
    lv_anim_set_values(&animation, LV_OPA_50, LV_OPA_100);
    lv_anim_set_duration(&animation, 650);
    lv_anim_set_playback_duration(&animation, 650);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
    slot.pulsing = true;
}

void stop_pulse(SlotWidget& slot)
{
    if (!slot.pulsing || slot.card == nullptr) {
        return;
    }

    lv_anim_delete(slot.card, pulse_exec);
    slot.pulsing = false;
}

void apply_slot_state(SlotWidget& slot, char state)
{
    if ((slot.card == nullptr) || (slot.state_label == nullptr)) {
        return;
    }

    slot.state = state;
    char text[2] = {state, '\0'};
    lv_label_set_text(slot.state_label, text);
    set_card_palette(slot.card, slot.state_label, state);

    if (state == 'R') {
        start_pulse(slot);
    } else {
        stop_pulse(slot);
    }
}

void update_texts()
{
    if (g_title_label != nullptr) {
        lv_label_set_text(g_title_label, ui_text::screen_title(g_language));
    }
    if (g_subtitle_label != nullptr) {
        lv_label_set_text(g_subtitle_label, "");
    }
    if (g_note_label != nullptr) {
        lv_label_set_text(g_note_label, "");
    }
    if (g_footer_label != nullptr) {
        lv_label_set_text(g_footer_label, g_footer_text);
    }
}

void set_placeholder_states()
{
    for (auto& slot : g_slots) {
        apply_slot_state(slot, '?');
    }
}

char slot_state_glyph_from_text(const char* value)
{
    if (value == nullptr || value[0] == '\0') {
        return '?';
    }
    if (std::strcmp(value, "running") == 0) {
        return 'R';
    }
    if (std::strcmp(value, "faulted") == 0) {
        return 'F';
    }
    if (std::strcmp(value, "stopped") == 0) {
        return 'S';
    }
    if (std::strcmp(value, "loaded") == 0) {
        return 'L';
    }
    if (std::strcmp(value, "empty") == 0) {
        return '-';
    }
    return '?';
}

char slot_state_glyph_from_fallback(bool loaded, bool run_enabled, bool have_status, uint32_t status_bits)
{
    if (!loaded) {
        return '-';
    }
    if (have_status && ((status_bits & kPlcSlotStatusFaulted) != 0u)) {
        return 'F';
    }
    if (run_enabled || (have_status && (status_bits == kPlcSlotStatusRunning))) {
        return 'R';
    }
    return 'L';
}

void update_slots_from_snapshot()
{
    bool have_live_state = false;
    for (uint8_t index = 0u; index < kSlotCount; ++index) {
        char feature[16] = {};
        char state_text[plclink::kStateStringInlineCapacity] = {};
        bool loaded = false;
        bool run_enabled = false;
        uint32_t status_bits = 0u;
        bool have_loaded = false;
        bool have_run_enabled = false;
        bool have_status = false;
        std::snprintf(feature, sizeof(feature), "plc.slot%u", static_cast<unsigned>(kSlotStart + index));
        if (spi_link::copy_string_state_by_path(feature, "state", state_text, sizeof(state_text))) {
            apply_slot_state(g_slots[index], slot_state_glyph_from_text(state_text));
            have_live_state = true;
        } else {
            have_loaded = spi_link::copy_bool_state_by_path(feature, "loaded", &loaded);
            have_run_enabled = spi_link::copy_bool_state_by_path(feature, "runEnabled", &run_enabled);
            have_status = spi_link::copy_u32_state_by_path(feature, "status", &status_bits);
            if (have_loaded || have_run_enabled || have_status) {
                apply_slot_state(g_slots[index],
                                 slot_state_glyph_from_fallback(loaded,
                                                               run_enabled,
                                                               have_status,
                                                               status_bits));
                have_live_state = true;
            } else {
                apply_slot_state(g_slots[index], '?');
            }
        }
    }

    g_have_live_slot_states = have_live_state;
    if (g_note_label != nullptr) {
        lv_label_set_text(g_note_label, "");
    }
}

void update_footer(const wifi_manager::Status& wifi)
{
    if (wifi.sta_ip[0] != '\0') {
        std::snprintf(g_footer_text,
                      sizeof(g_footer_text),
                      "%s = %s",
                      wifi.sta_ssid[0] != '\0' ? wifi.sta_ssid : ui_text::footer_ip_label(g_language),
                      wifi.sta_ip);
    } else {
        std::snprintf(g_footer_text,
                      sizeof(g_footer_text),
                      "%s %s %s",
                      ui_text::footer_ap_label(g_language),
                      wifi.ap_ssid,
                      wifi.ap_ip);
    }

    if (g_footer_label != nullptr) {
        lv_label_set_text(g_footer_label, g_footer_text);
    }
}

} // namespace

esp_err_t init()
{
    if (g_screen != nullptr) {
        return ESP_OK;
    }

    constexpr lv_coord_t kScreenWidth = app_config::kDisplayWidth;
    constexpr lv_coord_t kScreenHeight = app_config::kDisplayHeight;

    g_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_screen);
    lv_obj_set_size(g_screen, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x09111F), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

    g_title_label = lv_label_create(g_screen);
    lv_obj_set_style_text_color(g_title_label, lv_color_hex(0xF7FBFF), 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(g_title_label, 6, 3);

    g_subtitle_label = lv_label_create(g_screen);
    lv_obj_set_style_text_color(g_subtitle_label, lv_color_hex(0x91A6C6), 0);
    lv_obj_set_width(g_subtitle_label, 1);
    lv_label_set_long_mode(g_subtitle_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(g_subtitle_label, 0, 0);
    lv_obj_add_flag(g_subtitle_label, LV_OBJ_FLAG_HIDDEN);

    g_note_label = lv_label_create(g_screen);
    lv_obj_set_style_text_color(g_note_label, lv_color_hex(0x6F819C), 0);
    lv_obj_set_width(g_note_label, 1);
    lv_label_set_long_mode(g_note_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(g_note_label, 0, 0);
    lv_obj_add_flag(g_note_label, LV_OBJ_FLAG_HIDDEN);

    constexpr lv_coord_t kGridTop = 18;
    constexpr lv_coord_t kCardWidth = 31;
    constexpr lv_coord_t kCardHeight = 19;
    constexpr lv_coord_t kGridGapX = 3;
    constexpr lv_coord_t kGridGapY = 3;
    constexpr lv_coord_t kGridLeft = 4;

    for (uint8_t index = 0u; index < kSlotCount; ++index) {
        SlotWidget& slot = g_slots[index];
        slot.card = lv_obj_create(g_screen);
        lv_obj_remove_style_all(slot.card);
        lv_obj_set_size(slot.card, kCardWidth, kCardHeight);
        const lv_coord_t col = static_cast<lv_coord_t>(index % kSlotCols);
        const lv_coord_t row = static_cast<lv_coord_t>(index / kSlotCols);
        lv_obj_set_pos(slot.card,
                       static_cast<lv_coord_t>(kGridLeft + col * (kCardWidth + kGridGapX)),
                       static_cast<lv_coord_t>(kGridTop + row * (kCardHeight + kGridGapY)));
        lv_obj_set_style_radius(slot.card, 10, 0);
        lv_obj_set_style_border_width(slot.card, 2, 0);
        lv_obj_set_style_pad_all(slot.card, 0, 0);

        slot.slot_label = lv_label_create(slot.card);
        lv_obj_set_style_text_color(slot.slot_label, lv_color_hex(0xE5ECFA), 0);
        lv_obj_set_style_text_font(slot.slot_label, &lv_font_montserrat_12, 0);
        char slot_text[2] = {slot_hex_digit(static_cast<uint8_t>(kSlotStart + index)), '\0'};
        lv_label_set_text(slot.slot_label, slot_text);
        lv_obj_set_pos(slot.slot_label, 4, 1);

        slot.state_label = lv_label_create(slot.card);
        lv_obj_set_style_text_font(slot.state_label, &lv_font_montserrat_12, 0);
        lv_obj_align(slot.state_label, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
        apply_slot_state(slot, '?');
    }

    g_footer_label = lv_label_create(g_screen);
    lv_obj_set_width(g_footer_label, kScreenWidth - 8);
    lv_obj_set_style_text_color(g_footer_label, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_font(g_footer_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_footer_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(g_footer_label, 4, kScreenHeight - 14);

    update_texts();
    set_placeholder_states();
    return ESP_OK;
}

void show()
{
    if (g_screen == nullptr) {
        return;
    }

    lv_screen_load(g_screen);
}

void update(const char* language, const wifi_manager::Status& wifi)
{
    std::snprintf(g_language, sizeof(g_language), "%s", ui_text::normalize_language(language));
    update_footer(wifi);
    update_texts();
    update_slots_from_snapshot();
}

} // namespace status_ui