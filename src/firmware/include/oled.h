#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "bigsister.h"
#include "plc_loader_v1.h"
#include "plc_runtime_abi.h"
#include "u8g2.h"
#include "u8g2_hal.h"

namespace oled {

inline constexpr uint8_t kConsoleLines = 5;
inline constexpr uint8_t kConsoleCols = 18;
inline constexpr uint8_t kSlotIconCount = 4;
inline constexpr uint8_t kSlotIconWidth = 18;
inline constexpr uint8_t kSlotIconHeight = 12;
inline constexpr uint8_t kSlotIconX = 108;
inline constexpr uint8_t kSlotIconY = 0;
inline constexpr uint32_t kSlotRefreshMs = 250u;
inline constexpr uint32_t kRunningBlinkPeriodMs = 1000u;

inline u8g2_t device = {};
inline bool ready = false;
inline char console[kConsoleLines][kConsoleCols + 1] = {};
inline uint8_t console_count = 0u;

enum class ScreenId : uint8_t {
    BootConsole = 0,
    SlotStatus = 1,
    BootProgress = 2,
};

enum class PlcSlotIconSource : uint8_t {
    None = 0,
    Flash,
    Local,
};

enum class PlcSlotIconState : uint8_t {
    Hidden = 0,
    Empty,
    Loaded,
    Running,
    Faulted,
};

struct PlcSlotIconStatus {
    bool visible = false;
    uint8_t slot_id = 0u;
    PlcSlotIconSource source = PlcSlotIconSource::None;
    PlcSlotIconState state = PlcSlotIconState::Hidden;
    uint32_t control_status = 0u;
    uint32_t cycle_counter = 0u;
    uint32_t fault_code = 0u;
    bool activity_pulse = false;
};

inline PlcSlotIconStatus slot_icons[kSlotIconCount] = {};
inline ScreenId screen = ScreenId::BootConsole;
inline uint32_t slot_refresh_deadline_ms = 0u;

inline char slotHexDigit(uint8_t value)
{
    static const char kHexDigits[] = "0123456789ABCDEF";
    return kHexDigits[value & 0x0Fu];
}

inline void drawPlcSlotIcons()
{
    if (!ready) {
        return;
    }

    u8g2_SetFont(&device, u8g2_font_6x12_tf);
    for (uint8_t display_index = 0; display_index < kSlotIconCount; ++display_index) {
        const PlcSlotIconStatus& icon = slot_icons[display_index];
        const uint8_t x = kSlotIconX;
        const uint8_t y = static_cast<uint8_t>(kSlotIconY + display_index * (kSlotIconHeight + 1u));

        u8g2_SetDrawColor(&device, 0);
        u8g2_DrawBox(&device, x, y, kSlotIconWidth, kSlotIconHeight);
        u8g2_SetDrawColor(&device, 1);

        if (!icon.visible || icon.state == PlcSlotIconState::Hidden) {
            continue;
        }

        u8g2_DrawFrame(&device, x, y, kSlotIconWidth, kSlotIconHeight);

        char slot_label[2] = {slotHexDigit(icon.slot_id), '\0'};
        u8g2_DrawStr(&device, static_cast<int16_t>(x + 2u), static_cast<int16_t>(y + 10u), slot_label);

        switch (icon.source) {
        case PlcSlotIconSource::Flash:
            u8g2_DrawBox(&device, static_cast<int16_t>(x + 12u), static_cast<int16_t>(y + 2u), 4u, 4u);
            break;
        case PlcSlotIconSource::Local:
            u8g2_DrawFrame(&device, static_cast<int16_t>(x + 12u), static_cast<int16_t>(y + 2u), 4u, 4u);
            break;
        case PlcSlotIconSource::None:
        default:
            u8g2_DrawPixel(&device, static_cast<int16_t>(x + 14u), static_cast<int16_t>(y + 4u));
            break;
        }

        switch (icon.state) {
        case PlcSlotIconState::Faulted:
            u8g2_DrawLine(&device, static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 8u),
                          static_cast<int16_t>(x + 15u), static_cast<int16_t>(y + 10u));
            u8g2_DrawLine(&device, static_cast<int16_t>(x + 15u), static_cast<int16_t>(y + 8u),
                          static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 10u));
            break;
        case PlcSlotIconState::Running:
            if (icon.activity_pulse) {
                u8g2_DrawDisc(&device, static_cast<int16_t>(x + 13u), static_cast<int16_t>(y + 9u), 2u, U8G2_DRAW_ALL);
            } else {
                u8g2_DrawCircle(&device, static_cast<int16_t>(x + 13u), static_cast<int16_t>(y + 9u), 2u, U8G2_DRAW_ALL);
            }
            break;
        case PlcSlotIconState::Loaded:
            u8g2_DrawBox(&device, static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 8u), 5u, 2u);
            break;
        case PlcSlotIconState::Empty:
            u8g2_DrawLine(&device, static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 9u),
                          static_cast<int16_t>(x + 15u), static_cast<int16_t>(y + 9u));
            break;
        case PlcSlotIconState::Hidden:
        default:
            break;
        }
    }
    u8g2_SetDrawColor(&device, 1);
}

inline void sendBufferWithPlcIcons()
{
    drawPlcSlotIcons();
    u8g2_SendBuffer(&device);
}

inline bool refreshPlcSlotIcon(uint8_t display_index)
{
    if (display_index >= kSlotIconCount) {
        return false;
    }

    PlcSlotIconStatus& icon = slot_icons[display_index];
    if (!icon.visible) {
        return false;
    }

    PlcSlotIconStatus next = icon;
    const bool running_blink_on = ((millis() / kRunningBlinkPeriodMs) & 0x1u) == 0u;
    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(icon.slot_id)));

    if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
        control_block->slot_id != icon.slot_id ||
        control_block->bytecode_size == 0u ||
        control_block->bytecode_base == 0u) {
        next.state = PlcSlotIconState::Empty;
        next.control_status = 0u;
        next.cycle_counter = 0u;
        next.fault_code = 0u;
        next.activity_pulse = false;
    } else {
        next.control_status = control_block->status;
        next.cycle_counter = control_block->cycle_counter;
        next.fault_code = control_block->fault_code;
        if ((control_block->status & 0x80000000u) != 0u) {
            next.state = PlcSlotIconState::Faulted;
            next.activity_pulse = false;
        } else if (control_block->status == 2u) {
            next.state = PlcSlotIconState::Running;
            next.activity_pulse = running_blink_on;
        } else {
            next.state = PlcSlotIconState::Loaded;
            next.activity_pulse = false;
        }
    }

    const bool changed = next.source != icon.source ||
                         next.state != icon.state ||
                         next.control_status != icon.control_status ||
                         next.cycle_counter != icon.cycle_counter ||
                         next.fault_code != icon.fault_code ||
                         next.activity_pulse != icon.activity_pulse;
    icon = next;
    return changed;
}

inline void refreshPlcSlotIconsIfDue(uint32_t now_ms)
{
    if (!ready || static_cast<int32_t>(now_ms - slot_refresh_deadline_ms) < 0) {
        return;
    }

    bool changed = false;
    for (uint8_t display_index = 0; display_index < kSlotIconCount; ++display_index) {
        if (refreshPlcSlotIcon(display_index)) {
            changed = true;
        }
    }

    if (changed) {
        sendBufferWithPlcIcons();
    }
    slot_refresh_deadline_ms = now_ms + kSlotRefreshMs;
}

inline bool plcEngineEnabled()
{
    const volatile uint32_t* status_reg = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(PLC_BASE));
    return (*status_reg & 0x1u) != 0u;
}

inline char plcSlotStateGlyphRaw(uint8_t slot_id)
{
    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
        control_block->slot_id != slot_id ||
        control_block->bytecode_size == 0u ||
        control_block->bytecode_base == 0u) {
        return '-';
    }
    if ((control_block->status & kPlcSlotStatusFaultedV1) != 0u) {
        return 'F';
    }
    if ((control_block->control & kPlcSlotControlPausedV1) != 0u) {
        return 'S';
    }
    if (control_block->status == kPlcSlotStatusRunningV1) {
        return 'R';
    }
    return 'L';
}

inline char plcSlotStateGlyph(uint8_t slot_id)
{
    const char raw = plcSlotStateGlyphRaw(slot_id);
    if (raw != 'R') {
        return raw;
    }

    return ((millis() / kRunningBlinkPeriodMs) & 0x1u) == 0u ? 'R' : ' ';
}

inline uint8_t loadedSlotCount()
{
    uint8_t count = 0u;
    for (uint16_t slot_id = 0u; slot_id < kPlcSlotCountV1; ++slot_id) {
        if (plcSlotStateGlyphRaw(static_cast<uint8_t>(slot_id)) != '-') {
            ++count;
        }
    }
    return count;
}

inline void formatBootStepLines(const char* step,
                                char (&line1)[21],
                                char (&line2)[21])
{
    constexpr size_t kBootStepCols = 20u;
    const char* text = (step != nullptr && step[0] != '\0') ? step : "Boot";
    const size_t text_len = std::strlen(text);

    line1[0] = '\0';
    line2[0] = '\0';

    if (text_len <= kBootStepCols) {
        std::snprintf(line1, sizeof(line1), "%s", text);
        return;
    }

    size_t split = kBootStepCols;
    while (split > 0u && text[split] != ' ') {
        --split;
    }
    if (split == 0u) {
        split = kBootStepCols;
    }

    std::snprintf(line1, sizeof(line1), "%.*s", static_cast<int>(split), text);

    size_t second_start = split;
    while (text[second_start] == ' ') {
        ++second_start;
    }

    const size_t remaining_len = text_len - second_start;
    if (remaining_len <= kBootStepCols) {
        std::snprintf(line2, sizeof(line2), "%s", text + second_start);
        return;
    }

    std::snprintf(line2,
                  sizeof(line2),
                  "%.*s...",
                  static_cast<int>(kBootStepCols - 3u),
                  text + second_start);
}

inline void drawSlotStatusScreen(uint8_t nodenet_addr)
{
    if (!ready) {
        return;
    }

    char line[kConsoleCols + 4] = {};
    u8g2_SetFont(&device, u8g2_font_6x12_tf);
    u8g2_ClearBuffer(&device);

    (void)std::snprintf(line,
                        sizeof(line),
                        "PLC:%s NN:%02X %u/16",
                        plcEngineEnabled() ? "ON" : "OFF",
                        static_cast<unsigned>(nodenet_addr),
                        static_cast<unsigned>(loadedSlotCount()));
    u8g2_SetDrawColor(&device, 1);
    u8g2_DrawBox(&device, 0, 0, 128, 12);
    u8g2_SetDrawColor(&device, 0);
    u8g2_DrawStr(&device, 2, 10, line);
    u8g2_SetDrawColor(&device, 1);

    for (uint8_t row = 0u; row < 4u; ++row) {
        const uint8_t slot0 = static_cast<uint8_t>(row * 4u);
        (void)std::snprintf(line,
                            sizeof(line),
                            "%c:%c %c:%c %c:%c %c:%c",
                            slotHexDigit(slot0 + 0u),
                            plcSlotStateGlyph(slot0 + 0u),
                            slotHexDigit(slot0 + 1u),
                            plcSlotStateGlyph(slot0 + 1u),
                            slotHexDigit(slot0 + 2u),
                            plcSlotStateGlyph(slot0 + 2u),
                            slotHexDigit(slot0 + 3u),
                            plcSlotStateGlyph(slot0 + 3u));
        u8g2_DrawStr(&device, 2, static_cast<int16_t>(23u + row * 13u), line);
    }

    u8g2_SendBuffer(&device);
}

inline void showBootProgress(const char* step, uint8_t percent)
{
    if (!ready) {
        return;
    }

    if (percent > 100u) {
        percent = 100u;
    }

    screen = ScreenId::BootProgress;

    char line[kConsoleCols + 8] = {};
    char step_line1[21] = {};
    char step_line2[21] = {};
    const uint8_t bar_x = 8u;
    const uint8_t bar_y = 44u;
    const uint8_t bar_w = 112u;
    const uint8_t bar_h = 12u;
    const uint8_t fill_w = static_cast<uint8_t>((static_cast<uint16_t>(bar_w - 2u) * percent) / 100u);
    formatBootStepLines(step, step_line1, step_line2);

    u8g2_SetFont(&device, u8g2_font_6x12_tf);
    u8g2_ClearBuffer(&device);
    u8g2_DrawStr(&device, 2, 10, "NodeNet SoC RISC-V");
    u8g2_SetFont(&device, u8g2_font_5x8_tf);
    u8g2_DrawStr(&device, 2, 21, "Startup");
    u8g2_DrawStr(&device, 2, 30, step_line1);
    if (step_line2[0] != '\0') {
        u8g2_DrawStr(&device, 2, 38, step_line2);
    }
    u8g2_DrawFrame(&device, bar_x, bar_y, bar_w, bar_h);
    if (fill_w > 0u) {
        u8g2_DrawBox(&device,
                     static_cast<int16_t>(bar_x + 1u),
                     static_cast<int16_t>(bar_y + 1u),
                     fill_w,
                     static_cast<uint8_t>(bar_h - 2u));
    }
    (void)std::snprintf(line, sizeof(line), "%3u%%", static_cast<unsigned>(percent));
    u8g2_DrawStr(&device, 48, 62, line);
    u8g2_SendBuffer(&device);
}

inline void showSlotStatusScreen(uint8_t nodenet_addr)
{
    screen = ScreenId::SlotStatus;
    slot_refresh_deadline_ms = millis();
    drawSlotStatusScreen(nodenet_addr);
}

inline void refreshScreenIfDue(uint32_t now_ms, uint8_t nodenet_addr)
{
    if (screen == ScreenId::SlotStatus) {
        if (static_cast<int32_t>(now_ms - slot_refresh_deadline_ms) < 0) {
            return;
        }
        drawSlotStatusScreen(nodenet_addr);
        slot_refresh_deadline_ms = now_ms + kSlotRefreshMs;
        return;
    }

    refreshPlcSlotIconsIfDue(now_ms);
}

inline bool init(uint8_t addr7 = 0x3C)
{
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&device, U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
    u8g2_SetI2CAddress(&device, static_cast<uint8_t>(addr7 << 1));
    u8g2_InitDisplay(&device);
    u8g2_SetPowerSave(&device, 0);
    u8g2_ClearBuffer(&device);
    u8g2_SendBuffer(&device);
    ready = true;
    slot_refresh_deadline_ms = millis();
    return true;
}

inline void write(const char* text)
{
    if (!ready || text == nullptr || screen != ScreenId::BootConsole) {
        return;
    }

    uint8_t i = 0u;
    uint8_t line_idx = 0u;

    if (console_count < kConsoleLines) {
        line_idx = console_count++;
    } else {
        for (uint8_t row = 1; row < kConsoleLines; ++row) {
            for (uint8_t col = 0; col <= kConsoleCols; ++col) {
                console[row - 1u][col] = console[row][col];
            }
        }
        line_idx = kConsoleLines - 1u;
    }

    while (i < kConsoleCols && text[i] != '\0') {
        console[line_idx][i] = text[i];
        ++i;
    }
    console[line_idx][i] = '\0';

    u8g2_SetFont(&device, u8g2_font_6x12_tf);
    u8g2_ClearBuffer(&device);
    for (uint8_t row = 0; row < console_count; ++row) {
        u8g2_DrawStr(&device, 2, 10 + row * 13, console[row]);
    }
    sendBufferWithPlcIcons();
}

inline void print(const char* fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    char line[64] = {};
    va_list args;
    va_start(args, fmt);
    (void)std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    write(line);
}

inline void drawLines(const char* line0,
                      const char* line1,
                      const char* line2,
                      const char* line3,
                      const char* line4)
{
    if (!ready) {
        return;
    }

    const char* lines[kConsoleLines] = {
        (line0 != nullptr) ? line0 : "",
        (line1 != nullptr) ? line1 : "",
        (line2 != nullptr) ? line2 : "",
        (line3 != nullptr) ? line3 : "",
        (line4 != nullptr) ? line4 : "",
    };

    u8g2_SetFont(&device, u8g2_font_6x12_tf);
    u8g2_ClearBuffer(&device);
    for (uint8_t row = 0; row < kConsoleLines; ++row) {
        u8g2_DrawStr(&device, 2, 10 + row * 13, lines[row]);
    }
    sendBufferWithPlcIcons();
}

}  // namespace oled
