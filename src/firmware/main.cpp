#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include "sdram.h"
#include "plc_linker_v1.h"
#include "plc_loader_v1.h"
#include "plc_runtime_abi.h"
#include <ArduinoJson.h>
#include "bigsister.h"
#include "led.h"
#include "flash.h"
#include "wb_sdram_test_master.h"
#include "i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"
#include "version.h"
#include "lib/nodenet/nodenet.h"
#include "ModbusMaster.h"

#include "nodenetCore.h"


static constexpr uint8_t MODBUS1_SLAVE_ADDR_DEFAULT = 0x01u;

// ════════════════════════════════════════════════════════════════════════════
// OLED Display (U8G2)
// ════════════════════════════════════════════════════════════════════════════
static u8g2_t g_oled;
static bool g_oled_ready = false;
static constexpr uint8_t kOledConsoleLines = 5;
static constexpr uint8_t kOledConsoleCols = 18;
static constexpr uint8_t kOledSlotIconCount = 4;
static constexpr uint8_t kOledSlotIconWidth = 18;
static constexpr uint8_t kOledSlotIconHeight = 12;
static constexpr uint8_t kOledSlotIconX = 108;
static constexpr uint8_t kOledSlotIconY = 0;
static constexpr uint32_t kOledSlotRefreshMs = 250u;
static char g_oled_console[kOledConsoleLines][kOledConsoleCols + 1] = {};
static uint8_t g_oled_console_count = 0;
static NodeNet* g_nodenet = nullptr;
static WbLed* g_led_yellow = nullptr;
static volatile bool g_nodenet_broadcast_pending = false;
static volatile bool g_nodenet_message_pending = false;
static volatile uint8_t g_nodenet_broadcast_src = 0u;
static volatile uint8_t g_nodenet_message_src = 0u;
static volatile uint16_t g_nodenet_broadcast_len = 0u;
static volatile uint16_t g_nodenet_message_len = 0u;
static volatile uint32_t g_nodenet_broadcast_count = 0u;
static volatile uint32_t g_nodenet_message_count = 0u;
static uint8_t g_nodenet_broadcast_data[kOledConsoleCols + 1] = {};
static uint8_t g_nodenet_message_data[kOledConsoleCols + 1] = {};

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

static PlcSlotIconStatus g_oled_slot_icons[kOledSlotIconCount] = {};
static uint32_t g_oled_slot_refresh_deadline_ms = 0u;

enum class SdramFwTestStage : uint8_t {
    Idle = 0,
    Write0Start,
    Write0Wait,
    Write1Start,
    Write1Wait,
    Read0Start,
    Read0Wait,
    Read1Start,
    Read1Wait,
    SlowStart,
    SlowRun,
    SlowStop,
    FastStart,
    FastRun,
    FastStop,
    Passed,
    Failed,
};

struct SdramCpuTrafficStats {
    uint32_t loops;
    uint32_t mismatches;
};

struct SdramFwTestContext {
    SdramFwTestStage stage;
    SdramFwTestStage fail_stage;
    uint32_t stage_deadline_ms;
    uint32_t next_oled_refresh_ms;
    SdramCpuTrafficStats cpu;
    WbSdramTestMaster::ArbSnapshot snapshot;
    uint32_t fail_reason;
};

static constexpr uint32_t kSdramMasterPattern0 = 0x13579BDFu;
static constexpr uint32_t kSdramMasterPattern1 = 0x2468ACE0u;
static constexpr uint32_t kSdramSlowRunMs = 2500u;
static constexpr uint32_t kSdramFastRunMs = 2000u;
static constexpr uint32_t kSdramRefreshMs = 250u;
static constexpr uint32_t kSdramOneShotTimeoutMs = 1000u;
static constexpr bool kEnableSdramBringupTest = false;
static constexpr bool kEnablePlcLinkerSelfTest = true;
static constexpr bool kEnablePlcFlashRoundTripSelfTest = false;
static constexpr uint32_t FLASH0_BASE = 0x10007000u;
static constexpr uint32_t kSdramFailTimeout = 1u;
static constexpr uint32_t kSdramFailAck = 2u;
static constexpr uint32_t kSdramFailMismatch = 3u;
static constexpr uint32_t kSdramFailCpuMismatch = 4u;
static constexpr uint32_t kSdramFailGrantM0 = 5u;
static constexpr uint32_t kSdramFailGrantM1 = 6u;

static char oled_slot_hex_digit(uint8_t value)
{
    static const char kHexDigits[] = "0123456789ABCDEF";
    return kHexDigits[value & 0x0Fu];
}

static void oled_draw_plc_slot_icons()
{
    if (!g_oled_ready) {
        return;
    }

    u8g2_SetFont(&g_oled, u8g2_font_6x12_tf);
    for (uint8_t display_index = 0; display_index < kOledSlotIconCount; ++display_index) {
        const PlcSlotIconStatus& icon = g_oled_slot_icons[display_index];
        const uint8_t x = kOledSlotIconX;
        const uint8_t y = static_cast<uint8_t>(kOledSlotIconY + display_index * (kOledSlotIconHeight + 1u));

        u8g2_SetDrawColor(&g_oled, 0);
        u8g2_DrawBox(&g_oled, x, y, kOledSlotIconWidth, kOledSlotIconHeight);
        u8g2_SetDrawColor(&g_oled, 1);

        if (!icon.visible || icon.state == PlcSlotIconState::Hidden) {
            continue;
        }

        u8g2_DrawFrame(&g_oled, x, y, kOledSlotIconWidth, kOledSlotIconHeight);

        char slot_label[2] = {oled_slot_hex_digit(icon.slot_id), '\0'};
        u8g2_DrawStr(&g_oled, static_cast<int16_t>(x + 2u), static_cast<int16_t>(y + 10u), slot_label);

        switch (icon.source) {
        case PlcSlotIconSource::Flash:
            u8g2_DrawBox(&g_oled, static_cast<int16_t>(x + 12u), static_cast<int16_t>(y + 2u), 4u, 4u);
            break;
        case PlcSlotIconSource::Local:
            u8g2_DrawFrame(&g_oled, static_cast<int16_t>(x + 12u), static_cast<int16_t>(y + 2u), 4u, 4u);
            break;
        case PlcSlotIconSource::None:
        default:
            u8g2_DrawPixel(&g_oled, static_cast<int16_t>(x + 14u), static_cast<int16_t>(y + 4u));
            break;
        }

        switch (icon.state) {
        case PlcSlotIconState::Faulted:
            u8g2_DrawLine(&g_oled, static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 8u),
                          static_cast<int16_t>(x + 15u), static_cast<int16_t>(y + 10u));
            u8g2_DrawLine(&g_oled, static_cast<int16_t>(x + 15u), static_cast<int16_t>(y + 8u),
                          static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 10u));
            break;
        case PlcSlotIconState::Running:
            if (icon.activity_pulse) {
                u8g2_DrawDisc(&g_oled, static_cast<int16_t>(x + 13u), static_cast<int16_t>(y + 9u), 2u, U8G2_DRAW_ALL);
            } else {
                u8g2_DrawCircle(&g_oled, static_cast<int16_t>(x + 13u), static_cast<int16_t>(y + 9u), 2u, U8G2_DRAW_ALL);
            }
            break;
        case PlcSlotIconState::Loaded:
            u8g2_DrawBox(&g_oled, static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 8u), 5u, 2u);
            break;
        case PlcSlotIconState::Empty:
            u8g2_DrawLine(&g_oled, static_cast<int16_t>(x + 11u), static_cast<int16_t>(y + 9u),
                          static_cast<int16_t>(x + 15u), static_cast<int16_t>(y + 9u));
            break;
        case PlcSlotIconState::Hidden:
        default:
            break;
        }
    }
    u8g2_SetDrawColor(&g_oled, 1);
}

static void oled_send_buffer_with_plc_icons()
{
    oled_draw_plc_slot_icons();
    u8g2_SendBuffer(&g_oled);
}

static void oled_configure_plc_slot_icon(uint8_t display_index, uint8_t slot_id, PlcSlotIconSource source)
{
    if (display_index >= kOledSlotIconCount) {
        return;
    }

    PlcSlotIconStatus& icon = g_oled_slot_icons[display_index];
    icon.visible = true;
    icon.slot_id = slot_id;
    icon.source = source;
    icon.state = PlcSlotIconState::Empty;
    icon.control_status = 0u;
    icon.cycle_counter = 0u;
    icon.fault_code = 0u;
    icon.activity_pulse = false;
}

static bool oled_refresh_plc_slot_icon(uint8_t display_index)
{
    if (display_index >= kOledSlotIconCount) {
        return false;
    }

    PlcSlotIconStatus& icon = g_oled_slot_icons[display_index];
    if (!icon.visible) {
        return false;
    }

    PlcSlotIconStatus next = icon;
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
        const uint32_t old_cycle_counter = icon.cycle_counter;
        next.control_status = control_block->status;
        next.cycle_counter = control_block->cycle_counter;
        next.fault_code = control_block->fault_code;
        if ((control_block->status & 0x80000000u) != 0u) {
            next.state = PlcSlotIconState::Faulted;
            next.activity_pulse = false;
        } else if (control_block->status == 2u) {
            next.state = PlcSlotIconState::Running;
            if (control_block->cycle_counter != old_cycle_counter) {
                next.activity_pulse = !icon.activity_pulse;
            }
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

static void oled_refresh_plc_slot_icons_if_due(uint32_t now_ms)
{
    if (!g_oled_ready || (int32_t)(now_ms - g_oled_slot_refresh_deadline_ms) < 0) {
        return;
    }

    bool changed = false;
    for (uint8_t display_index = 0; display_index < kOledSlotIconCount; ++display_index) {
        if (oled_refresh_plc_slot_icon(display_index)) {
            changed = true;
        }
    }

    if (changed) {
        oled_send_buffer_with_plc_icons();
    }
    g_oled_slot_refresh_deadline_ms = now_ms + kOledSlotRefreshMs;
}

static bool oled_init(uint8_t addr7 = 0x3C)
{
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&g_oled, U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
    u8g2_SetI2CAddress(&g_oled, static_cast<uint8_t>(addr7 << 1));
    u8g2_InitDisplay(&g_oled);
    u8g2_SetPowerSave(&g_oled, 0);
    u8g2_ClearBuffer(&g_oled);
    u8g2_SendBuffer(&g_oled);
    g_oled_ready = true;
    return true;
}
// Console-style OLED write: append one line and scroll when full.
static void oled_write(const char* text)
{
    if (!g_oled_ready || text == nullptr) {
        return;
    }

    uint8_t i = 0;
    uint8_t line_idx;

    if (g_oled_console_count < kOledConsoleLines) {
        line_idx = g_oled_console_count++;
    } else {
        for (uint8_t row = 1; row < kOledConsoleLines; ++row) {
            for (uint8_t col = 0; col <= kOledConsoleCols; ++col) {
                g_oled_console[row - 1][col] = g_oled_console[row][col];
            }
        }
        line_idx = kOledConsoleLines - 1;
    }

    while (i < kOledConsoleCols && text[i] != '\0') {
        g_oled_console[line_idx][i] = text[i];
        ++i;
    }
    g_oled_console[line_idx][i] = '\0';

    u8g2_SetFont(&g_oled, u8g2_font_6x12_tf);
    u8g2_ClearBuffer(&g_oled);
    for (uint8_t row = 0; row < g_oled_console_count; ++row) {
        u8g2_DrawStr(&g_oled, 2, 10 + row * 13, g_oled_console[row]);
    }
    oled_send_buffer_with_plc_icons();

}

// printf-style OLED write helper.
static void oled_print(const char* fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    char line[64] = {};
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    oled_write(line);
}

static void oled_boot_status(uint8_t line, const char* text)
{
    (void)line;
    oled_write(text);
}

static void oled_draw_lines(const char* line0,
                            const char* line1,
                            const char* line2,
                            const char* line3,
                            const char* line4)
{
    if (!g_oled_ready) {
        return;
    }

    const char* lines[kOledConsoleLines] = {
        (line0 != nullptr) ? line0 : "",
        (line1 != nullptr) ? line1 : "",
        (line2 != nullptr) ? line2 : "",
        (line3 != nullptr) ? line3 : "",
        (line4 != nullptr) ? line4 : "",
    };

    u8g2_SetFont(&g_oled, u8g2_font_6x12_tf);
    u8g2_ClearBuffer(&g_oled);
    for (uint8_t row = 0; row < kOledConsoleLines; ++row) {
        u8g2_DrawStr(&g_oled, 2, 10 + row * 13, lines[row]);
    }
    oled_send_buffer_with_plc_icons();
}

static void hex32_to_str(uint32_t value, char* out)
{
    static const char kHex[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t nib = (uint8_t)((value >> ((7u - i) * 4u)) & 0xFu);
        out[2u + i] = kHex[nib];
    }
    out[10] = '\0';
}

static void oled_print_rx_header(uint8_t src, uint16_t len)
{
    static const char kHex[] = "0123456789ABCDEF";
    char line[22] = "[NN] RX 00 L00000";

    line[8] = kHex[(src >> 4) & 0x0F];
    line[9] = kHex[src & 0x0F];

    // Decimal len, right-aligned on 5 chars.
    uint16_t v = len;
    for (int i = 0; i < 5; ++i) {
        line[16 - i] = (char)('0' + (v % 10u));
        v /= 10u;
    }

    oled_write(line);
}

static void format_version_hundredths(uint16_t raw_value, char* out, std::size_t out_size)
{
    if (out == nullptr || out_size == 0u) {
        return;
    }

    const unsigned major = static_cast<unsigned>(raw_value / 100u);
    const unsigned minor = static_cast<unsigned>(raw_value % 100u);
    (void)snprintf(out, out_size, "V%u.%02u", major, minor);
}

static void oled_write_payload_safe(const uint8_t* data, uint16_t len)
{
    if (data == nullptr) {
        oled_write("[NN] <null>");
        return;
    }

    uint16_t text_len = len;
    if (text_len > 0 && data[text_len - 1] == 0u) {
        text_len -= 1u;
    }

    if (text_len > kOledConsoleCols) {
        text_len = kOledConsoleCols;
    }

    char line[kOledConsoleCols + 1] = {};
    for (uint16_t i = 0; i < text_len; ++i) {
        const uint8_t c = data[i];
        line[i] = (c >= 32u && c <= 126u) ? (char)c : '.';
    }
    line[text_len] = '\0';
    oled_write(line);
}

static void nodenet_copy_payload_snapshot(volatile uint8_t* dst,
                                          const uint8_t* src,
                                          uint16_t len)
{
    uint16_t copy_len = len;
    if (copy_len > kOledConsoleCols) {
        copy_len = kOledConsoleCols;
    }

    for (uint16_t i = 0; i < copy_len; ++i) {
        dst[i] = (src != nullptr) ? src[i] : 0u;
    }
    dst[copy_len] = 0u;
}

static void led_d2_blink()
{
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    delay(500u);
}

static const char* sdram_test_stage_name(SdramFwTestStage stage)
{
    switch (stage) {
        case SdramFwTestStage::Idle: return "idle";
        case SdramFwTestStage::Write0Start: return "wr0 start";
        case SdramFwTestStage::Write0Wait: return "wr0 wait";
        case SdramFwTestStage::Write1Start: return "wr1 start";
        case SdramFwTestStage::Write1Wait: return "wr1 wait";
        case SdramFwTestStage::Read0Start: return "rd0 start";
        case SdramFwTestStage::Read0Wait: return "rd0 wait";
        case SdramFwTestStage::Read1Start: return "rd1 start";
        case SdramFwTestStage::Read1Wait: return "rd1 wait";
        case SdramFwTestStage::SlowStart: return "slow start";
        case SdramFwTestStage::SlowRun: return "slow run";
        case SdramFwTestStage::SlowStop: return "slow stop";
        case SdramFwTestStage::FastStart: return "fast start";
        case SdramFwTestStage::FastRun: return "fast run";
        case SdramFwTestStage::FastStop: return "fast stop";
        case SdramFwTestStage::Passed: return "pass";
        case SdramFwTestStage::Failed: return "fail";
        default: return "?";
    }
}

static const char* sdram_fail_reason_name(uint32_t reason)
{
    switch (reason) {
        case 0u: return "none";
        case kSdramFailTimeout: return "timeout";
        case kSdramFailAck: return "ack";
        case kSdramFailMismatch: return "mismatch";
        case kSdramFailCpuMismatch: return "cpu mm";
        case kSdramFailGrantM0: return "grant m0";
        case kSdramFailGrantM1: return "grant m1";
        default: return "other";
    }
}

static void sdram_cpu_traffic_tick(SdramCpuTrafficStats* stats, uint32_t burst_count)
{
    if (stats == nullptr) {
        return;
    }

    volatile uint32_t* scratch = sdram_test_scratch_words();

    for (uint32_t burst = 0; burst < burst_count; ++burst) {
        const uint32_t idx = 64u + (stats->loops & 0x0Fu);
        const uint32_t pattern = 0xC0DEC000u ^ (stats->loops * 17u) ^ idx;
        scratch[idx] = pattern;
        if (scratch[idx] != pattern) {
            ++stats->mismatches;
        }
        ++stats->loops;
    }
}

static void oled_show_sdram_test_page(const char* phase,
                                      const WbSdramTestMaster& master,
                                      const WbSdramTestMaster::ArbSnapshot& arb,
                                      const SdramCpuTrafficStats& cpu,
                                      uint32_t fail_reason,
                                      SdramFwTestStage fail_stage)
{
    char line0[22] = {};
    char line1[22] = {};
    char line2[22] = {};
    char line3[22] = {};
    char line4[22] = {};

    if (fail_reason == 0u) {
        (void)snprintf(line0, sizeof(line0), "[SDRAM] %s", phase);
    } else {
        (void)snprintf(line0, sizeof(line0), "[SDRAM] %s", sdram_fail_reason_name(fail_reason));
    }
    (void)snprintf(line1, sizeof(line1), "m0=%lu m1=%lu",
                   static_cast<unsigned long>(arb.m0Grants),
                   static_cast<unsigned long>(arb.m1Grants));
    (void)snprintf(line2, sizeof(line2), "s0=%lu s1=%lu",
                   static_cast<unsigned long>(arb.m0Stalls),
                   static_cast<unsigned long>(arb.m1Stalls));
    (void)snprintf(line3, sizeof(line3), "ack=%lu mm=%lu",
                   static_cast<unsigned long>(arb.ackCount),
                   static_cast<unsigned long>(master.mismatchCount()));
    if (fail_reason == 0u) {
        (void)snprintf(line4, sizeof(line4), "cpu=%lu e=%lu",
                       static_cast<unsigned long>(cpu.loops),
                       static_cast<unsigned long>(cpu.mismatches));
    } else {
        (void)snprintf(line4, sizeof(line4), "%s r=%lu",
                       sdram_test_stage_name(fail_stage),
                       static_cast<unsigned long>(fail_reason));
    }

    oled_draw_lines(line0, line1, line2, line3, line4);
}

static void sdram_configure_one_shot_write(WbSdramTestMaster& master,
                                           uint32_t addr,
                                           uint32_t value)
{
    master.stop();
    master.setAddress0(addr);
    master.setAddress1(addr);
    master.setWriteData0(value);
    master.setWriteData1(value);
    master.setExpect0(value);
    master.setExpect1(value);
    master.setIntervalMs(0u);
    master.configure(false, true, false, false);
}

static void sdram_configure_one_shot_read(WbSdramTestMaster& master,
                                          uint32_t addr,
                                          uint32_t expected)
{
    master.stop();
    master.setAddress0(addr);
    master.setAddress1(addr);
    master.setWriteData0(expected);
    master.setWriteData1(expected);
    master.setExpect0(expected);
    master.setExpect1(expected);
    master.setIntervalMs(0u);
    master.configure(false, false, false, true);
}

static void sdram_configure_compare_read(WbSdramTestMaster& master,
                                         uint32_t addr0,
                                         uint32_t addr1,
                                         uint32_t interval_ms,
                                         bool continuous,
                                         bool alternate)
{
    master.stop();
    master.setAddress0(addr0);
    master.setAddress1(addr1);
    master.setWriteData0(kSdramMasterPattern0);
    master.setWriteData1(kSdramMasterPattern1);
    master.setExpect0(kSdramMasterPattern0);
    master.setExpect1(kSdramMasterPattern1);
    master.setIntervalMs(interval_ms);
    master.configure(continuous, false, alternate, true);
}

static void sdram_fail(SdramFwTestContext* ctx,
                       WbLed& fail_led,
                       SdramFwTestStage stage,
                       uint32_t reason)
{
    if (ctx == nullptr) {
        return;
    }
    ctx->fail_stage = stage;
    ctx->fail_reason = reason;
    ctx->stage = SdramFwTestStage::Failed;
    fail_led.on();
}

static void sdram_begin_slow_test(SdramFwTestContext* ctx,
                                  WbSdramTestMaster& master,
                                  uint32_t addr0,
                                  uint32_t addr1,
                                  uint32_t now_ms)
{
    if (ctx == nullptr) {
        return;
    }

    sdram_configure_compare_read(master, addr0, addr1, 200u, true, true);
    master.clearCounters();
    master.start();
    ctx->stage = SdramFwTestStage::SlowRun;
    ctx->stage_deadline_ms = now_ms + kSdramSlowRunMs;
}

static void sdram_begin_fast_test(SdramFwTestContext* ctx,
                                  WbSdramTestMaster& master,
                                  uint32_t addr0,
                                  uint32_t addr1,
                                  uint32_t now_ms)
{
    if (ctx == nullptr) {
        return;
    }

    sdram_configure_compare_read(master, addr0, addr1, 0u, true, true);
    master.clearCounters();
    master.start();
    ctx->cpu.loops = 0u;
    ctx->cpu.mismatches = 0u;
    ctx->stage = SdramFwTestStage::FastRun;
    ctx->stage_deadline_ms = now_ms + kSdramFastRunMs;
}

static void sdram_refresh_snapshot(SdramFwTestContext* ctx, const WbSdramTestMaster& master)
{
    if (ctx == nullptr) {
        return;
    }
    ctx->snapshot = master.arbSnapshot();
}

static void sdram_update_test(SdramFwTestContext* ctx,
                              WbSdramTestMaster& master,
                              uint32_t addr0,
                              uint32_t addr1,
                              uint32_t now_ms,
                              WbLed& pass_led,
                              WbLed& fail_led)
{
    if (ctx == nullptr) {
        return;
    }

    switch (ctx->stage) {
        case SdramFwTestStage::Idle:
            ctx->stage = SdramFwTestStage::Write0Start;
            break;

        case SdramFwTestStage::Write0Start:
            sdram_configure_one_shot_write(master, addr0, kSdramMasterPattern0);
            master.clearCounters();
            master.start();
            ctx->stage = SdramFwTestStage::Write0Wait;
            ctx->stage_deadline_ms = now_ms + kSdramOneShotTimeoutMs;
            break;

        case SdramFwTestStage::Write0Wait:
            if (!master.active()) {
                if (master.ackedCount() >= 1u) {
                    ctx->stage = SdramFwTestStage::Write1Start;
                } else {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::Write0Wait, kSdramFailAck);
                }
            } else if ((int32_t)(now_ms - ctx->stage_deadline_ms) >= 0) {
                master.stop();
                sdram_fail(ctx, fail_led, SdramFwTestStage::Write0Wait, kSdramFailTimeout);
            }
            break;

        case SdramFwTestStage::Write1Start:
            sdram_configure_one_shot_write(master, addr1, kSdramMasterPattern1);
            master.clearCounters();
            master.start();
            ctx->stage = SdramFwTestStage::Write1Wait;
            ctx->stage_deadline_ms = now_ms + kSdramOneShotTimeoutMs;
            break;

        case SdramFwTestStage::Write1Wait:
            if (!master.active()) {
                if (master.ackedCount() >= 1u) {
                    ctx->stage = SdramFwTestStage::Read0Start;
                } else {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::Write1Wait, kSdramFailAck);
                }
            } else if ((int32_t)(now_ms - ctx->stage_deadline_ms) >= 0) {
                master.stop();
                sdram_fail(ctx, fail_led, SdramFwTestStage::Write1Wait, kSdramFailTimeout);
            }
            break;

        case SdramFwTestStage::Read0Start:
            sdram_configure_one_shot_read(master, addr0, kSdramMasterPattern0);
            master.clearCounters();
            master.start();
            ctx->stage = SdramFwTestStage::Read0Wait;
            ctx->stage_deadline_ms = now_ms + kSdramOneShotTimeoutMs;
            break;

        case SdramFwTestStage::Read0Wait:
            if (!master.active()) {
                if (master.ackedCount() >= 1u && !master.mismatchSticky()) {
                    ctx->stage = SdramFwTestStage::Read1Start;
                } else if (master.mismatchSticky()) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::Read0Wait, kSdramFailMismatch);
                } else {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::Read0Wait, kSdramFailAck);
                }
            } else if ((int32_t)(now_ms - ctx->stage_deadline_ms) >= 0) {
                master.stop();
                sdram_fail(ctx, fail_led, SdramFwTestStage::Read0Wait, kSdramFailTimeout);
            }
            break;

        case SdramFwTestStage::Read1Start:
            sdram_configure_one_shot_read(master, addr1, kSdramMasterPattern1);
            master.clearCounters();
            master.start();
            ctx->stage = SdramFwTestStage::Read1Wait;
            ctx->stage_deadline_ms = now_ms + kSdramOneShotTimeoutMs;
            break;

        case SdramFwTestStage::Read1Wait:
            if (!master.active()) {
                sdram_refresh_snapshot(ctx, master);
                if (master.ackedCount() >= 1u && !master.mismatchSticky()) {
                    pass_led.blink(120u);
                    ctx->stage = SdramFwTestStage::SlowStart;
                } else if (master.mismatchSticky()) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::Read1Wait, kSdramFailMismatch);
                } else {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::Read1Wait, kSdramFailAck);
                }
            } else if ((int32_t)(now_ms - ctx->stage_deadline_ms) >= 0) {
                master.stop();
                sdram_fail(ctx, fail_led, SdramFwTestStage::Read1Wait, kSdramFailTimeout);
            }
            break;

        case SdramFwTestStage::SlowStart:
            sdram_begin_slow_test(ctx, master, addr0, addr1, now_ms);
            break;

        case SdramFwTestStage::SlowRun:
            sdram_cpu_traffic_tick(&ctx->cpu, 1u);
            if ((int32_t)(now_ms - ctx->stage_deadline_ms) >= 0) {
                master.stop();
                ctx->stage = SdramFwTestStage::SlowStop;
            }
            break;

        case SdramFwTestStage::SlowStop:
            sdram_cpu_traffic_tick(&ctx->cpu, 1u);
            if (!master.active()) {
                sdram_refresh_snapshot(ctx, master);
                if (master.mismatchSticky()) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::SlowStop, kSdramFailMismatch);
                } else if (ctx->snapshot.m0Grants == 0u) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::SlowStop, kSdramFailGrantM0);
                } else if (ctx->snapshot.m1Grants == 0u) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::SlowStop, kSdramFailGrantM1);
                } else {
                    pass_led.blink(120u);
                    ctx->stage = SdramFwTestStage::FastStart;
                }
            }
            break;

        case SdramFwTestStage::FastStart:
            sdram_begin_fast_test(ctx, master, addr0, addr1, now_ms);
            break;

        case SdramFwTestStage::FastRun:
            sdram_cpu_traffic_tick(&ctx->cpu, 8u);
            if ((int32_t)(now_ms - ctx->stage_deadline_ms) >= 0) {
                master.stop();
                ctx->stage = SdramFwTestStage::FastStop;
            }
            break;

        case SdramFwTestStage::FastStop:
            sdram_cpu_traffic_tick(&ctx->cpu, 2u);
            if (!master.active()) {
                sdram_refresh_snapshot(ctx, master);
                if (master.mismatchCount() != 0u) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::FastStop, kSdramFailMismatch);
                } else if (ctx->cpu.mismatches != 0u) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::FastStop, kSdramFailCpuMismatch);
                } else if (ctx->snapshot.m0Grants <= 32u) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::FastStop, kSdramFailGrantM0);
                } else if (ctx->snapshot.m1Grants <= 32u) {
                    sdram_fail(ctx, fail_led, SdramFwTestStage::FastStop, kSdramFailGrantM1);
                } else {
                    pass_led.on();
                    ctx->stage = SdramFwTestStage::Passed;
                }
            }
            break;

        case SdramFwTestStage::Passed:
        case SdramFwTestStage::Failed:
        default:
            break;
    }

    if ((int32_t)(now_ms - ctx->next_oled_refresh_ms) >= 0) {
        sdram_refresh_snapshot(ctx, master);
        oled_show_sdram_test_page(sdram_test_stage_name(ctx->stage),
                                  master,
                                  ctx->snapshot,
                                  ctx->cpu,
                                  ctx->fail_reason,
                                  ctx->fail_stage);
        ctx->next_oled_refresh_ms = now_ms + kSdramRefreshMs;
    }
}

struct PlcLinkerSelfTestResult {
    bool resolve_ok;
    bool link_ok;
    uint16_t resolved_points;
    uint16_t sample_relocations;
    PlcObjectLinkResultV1 link_result;
};

struct PlcLoaderSelfTestResult {
    bool parse_ok;
    bool load_ok;
    bool package_ok;
    bool flash_ok;
    uint16_t sample_symbols;
    PlcSlotLoadResultV1 load_result;
    PlcSlotLoadResultV1 package_load_result;
    PlcSlotLoadResultV1 flash_load_result;
};

struct PlcMirrorProgramLoadResult {
    bool load_ok;
    uint8_t channel;
    PlcSlotLoadResultV1 load_result;
};

enum class PlcSlot0BootSource : uint8_t {
    None = 0,
    FlashPackage,
    HardcodedMirror,
};

struct PlcSlot0BootResult {
    bool load_ok;
    bool flash_candidate_valid;
    uint8_t channel;
    PlcSlot0BootSource source;
    PlcSlotLoadResultV1 flash_load_result;
    PlcSlotLoadResultV1 fallback_load_result;
};

static bool plc_flash_erase_range(Flash& flash, uint32_t flash_offset, uint32_t size)
{
    if ((flash_offset % Flash::kSectorSize) != 0u || (size % Flash::kSectorSize) != 0u) {
        return false;
    }

    for (uint32_t offset = 0u; offset < size; offset += Flash::kSectorSize) {
        if (!flash.eraseSector(flash_offset + offset)) {
            return false;
        }
    }
    return true;
}

static bool plc_flash_write_erased_bytes(Flash& flash, uint32_t flash_offset, const uint8_t* data, size_t size)
{
    if (data == nullptr) {
        return false;
    }

    uint8_t page[Flash::kPageSize] = {};
    const uint32_t first_page_base = flash_offset & ~(Flash::kPageSize - 1u);
    const uint32_t first_page_offset = flash_offset - first_page_base;
    size_t consumed = 0u;
    uint32_t page_base = first_page_base;

    while (consumed < size) {
        std::memset(page, 0xFF, sizeof(page));
        const uint32_t page_offset = (page_base == first_page_base) ? first_page_offset : 0u;
        size_t chunk = Flash::kPageSize - page_offset;
        if (chunk > (size - consumed)) {
            chunk = size - consumed;
        }

        std::memcpy(page + page_offset, data + consumed, chunk);
        if (!flash.writePage(page_base, page)) {
            return false;
        }

        consumed += chunk;
        page_base += Flash::kPageSize;
    }

    return true;
}

static bool plc_verify_loaded_slot(uint16_t slot_id,
                                   size_t code_size,
                                   uint16_t sample_count,
                                   const uint16_t* expected_indices,
                                   uint32_t expected_max_instructions_per_scan,
                                   uint32_t expected_max_scan_time_us,
                                   uint32_t expected_epoch)
{
    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    const auto* directory_header = reinterpret_cast<const PlcSlotDirectoryHeaderV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotDirectoryHeaderAddress()));
    const auto* slot_manifest = reinterpret_cast<const PlcSlotManifestV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotManifestAddress(slot_id)));
    const PlcSlotLayoutV1 layout = PlcSlotLoaderV1::slotLayout(slot_id);
    const auto* linked_header = reinterpret_cast<const PlcLinkedImageHeaderV1*>(
        static_cast<uintptr_t>(layout.linked_image_header_addr));
    const uint8_t* linked_code = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(layout.linked_code_addr));

    if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
        control_block->slot_id != slot_id ||
        control_block->bytecode_size != code_size ||
        control_block->bytecode_base != layout.linked_code_addr ||
        control_block->stack_base != layout.stack_base ||
        control_block->timer_base != layout.timer_base ||
        control_block->max_instructions_per_scan != expected_max_instructions_per_scan ||
        control_block->max_scan_time_us != expected_max_scan_time_us) {
        return false;
    }

    if (linked_header->magic != kPlcLinkedImageMagicV1 ||
        linked_header->slot_id != slot_id ||
        linked_header->code_size != code_size ||
        linked_header->symbol_count != sample_count ||
        linked_header->relocation_count != sample_count ||
        linked_header->runtime_header_addr != kPlcRuntimeHeaderAddr) {
        return false;
    }

    if (directory_header->magic != kPlcSlotDirectoryMagicV1 ||
        directory_header->slot_count != kPlcSlotCountV1 ||
        directory_header->entry_size != sizeof(PlcSlotManifestV1) ||
        directory_header->directory_epoch != expected_epoch) {
        return false;
    }

    if (slot_manifest->slot_id != slot_id ||
        slot_manifest->status != kPlcSlotManifestStatusLoadedV1 ||
        slot_manifest->control_block_addr != PlcSlotLoaderV1::slotControlAddress(slot_id) ||
        slot_manifest->linked_image_header_addr != layout.linked_image_header_addr ||
        slot_manifest->linked_code_addr != layout.linked_code_addr ||
        slot_manifest->linked_code_size != code_size ||
        slot_manifest->stack_base != layout.stack_base ||
        slot_manifest->timer_base != layout.timer_base ||
        slot_manifest->scratch_base != layout.scratch_base ||
        slot_manifest->runtime_header_addr != kPlcRuntimeHeaderAddr ||
        slot_manifest->linked_code_checksum != linked_header->linked_code_checksum ||
        slot_manifest->load_epoch != expected_epoch) {
        return false;
    }

    for (uint16_t i = 0u; i < sample_count; ++i) {
        const uint16_t patched_index = static_cast<uint16_t>(linked_code[i * 2u]) |
                                       static_cast<uint16_t>(linked_code[i * 2u + 1u] << 8u);
        if (patched_index != expected_indices[i]) {
            return false;
        }
    }

    return true;
}

static PlcMirrorProgramLoadResult plc_load_hardcoded_mirror_program(const NodeNetCore& node_core,
                                                                    const PointCatalog& catalog,
                                                                    const PlcRuntimePublisherV1& publisher,
                                                                    uint8_t channel)
{
    PlcMirrorProgramLoadResult result = {};
    result.load_ok = false;
    result.channel = channel;

    if (channel == 0u || channel > 8u) {
        result.load_result.status = kPlcSlotLoadInvalidArgument;
        return result;
    }

    char input_point_id[16] = {};
    char output_point_id[16] = {};
    (void)snprintf(input_point_id, sizeof(input_point_id), "input%u", static_cast<unsigned>(channel));
    (void)snprintf(output_point_id, sizeof(output_point_id), "output%u", static_cast<unsigned>(channel));

    PlcObjectSymbolRecordV1 symbols[2] = {};
    std::strncpy(symbols[0].point_id.device_id, node_core.deviceId, sizeof(symbols[0].point_id.device_id) - 1u);
    std::strncpy(symbols[0].point_id.feature, "modbus0.waveshare8ch", sizeof(symbols[0].point_id.feature) - 1u);
    std::strncpy(symbols[0].point_id.point_id, input_point_id, sizeof(symbols[0].point_id.point_id) - 1u);
    symbols[0].expected_type = static_cast<uint8_t>(PointValueType::Bool);
    symbols[0].access = static_cast<uint8_t>(kPlcRuntimeLinkRead);

    std::strncpy(symbols[1].point_id.device_id, node_core.deviceId, sizeof(symbols[1].point_id.device_id) - 1u);
    std::strncpy(symbols[1].point_id.feature, "modbus0.waveshare8ch", sizeof(symbols[1].point_id.feature) - 1u);
    std::strncpy(symbols[1].point_id.point_id, output_point_id, sizeof(symbols[1].point_id.point_id) - 1u);
    symbols[1].expected_type = static_cast<uint8_t>(PointValueType::Bool);
    symbols[1].access = static_cast<uint8_t>(kPlcRuntimeLinkWrite);

    PlcObjectRelocationRecordV1 relocations[2] = {};
    relocations[0].code_offset = 1u;
    relocations[0].symbol_index = 0u;
    relocations[0].relocation_kind = kPlcRelocationPointIndexU16Le;
    relocations[1].code_offset = 4u;
    relocations[1].symbol_index = 1u;
    relocations[1].relocation_kind = kPlcRelocationPointIndexU16Le;

    uint8_t object_code[] = {
        0x10u, 0x00u, 0x00u,
        0x11u, 0x00u, 0x00u,
        0x00u,
    };

    PlcObjectImageV1 object_image = {};
    object_image.code_bytes = object_code;
    object_image.code_size = static_cast<uint32_t>(sizeof(object_code));
    object_image.entry_offset = 0u;
    object_image.symbols = symbols;
    object_image.symbol_count = 2u;
    object_image.relocations = relocations;
    object_image.relocation_count = 2u;

    result.load_result = PlcSlotLoaderV1::loadParsedObjectIntoSlot(publisher,
                                                                   catalog,
                                                                   0u,
                                                                   object_image,
                                                                   16u,
                                                                   5000u);
    result.load_ok = result.load_result.status == kPlcSlotLoadOk;
    return result;
}

static bool plc_slot_bytecode_supported_by_firmware_vm(uint16_t slot_id)
{
    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
        control_block->slot_id != slot_id ||
        control_block->bytecode_size == 0u ||
        control_block->bytecode_base == 0u) {
        return false;
    }

    const uint8_t* code = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(control_block->bytecode_base));
    uint32_t pc = 0u;
    while (pc < control_block->bytecode_size) {
        const uint8_t opcode = code[pc++];
        switch (opcode) {
        case 0x00u:
            return pc == control_block->bytecode_size;

        case 0x10u:
        case 0x11u:
            if ((pc + 1u) >= control_block->bytecode_size) {
                return false;
            }
            pc += 2u;
            break;

        default:
            return false;
        }
    }

    return false;
}

static PlcSlot0BootResult plc_boot_slot0_program(const NodeNetCore& node_core,
                                                 const PointCatalog& catalog,
                                                 const PlcRuntimePublisherV1& publisher,
                                                 Flash& flash,
                                                 uint8_t fallback_channel)
{
    PlcSlot0BootResult result = {};
    result.load_ok = false;
    result.flash_candidate_valid = false;
    result.channel = fallback_channel;
    result.source = PlcSlot0BootSource::None;
    result.flash_load_result.status = kPlcSlotLoadInvalidArgument;
    result.fallback_load_result.status = kPlcSlotLoadInvalidArgument;

    result.flash_load_result = PlcSlotLoaderV1::loadLinkedProgramPackageFromFlash(publisher,
                                                                                   flash,
                                                                                   0u,
                                                                                   Flash::kPlcPackageSlotBase,
                                                                                   Flash::kPlcPackageSlotSize);
    if (result.flash_load_result.status == kPlcSlotLoadOk &&
        plc_slot_bytecode_supported_by_firmware_vm(0u)) {
        result.load_ok = true;
        result.flash_candidate_valid = true;
        result.source = PlcSlot0BootSource::FlashPackage;
        return result;
    }

    const PlcMirrorProgramLoadResult fallback = plc_load_hardcoded_mirror_program(node_core,
                                                                                   catalog,
                                                                                   publisher,
                                                                                   fallback_channel);
    result.fallback_load_result = fallback.load_result;
    result.load_ok = fallback.load_ok;
    if (fallback.load_ok) {
        result.source = PlcSlot0BootSource::HardcodedMirror;
    }
    return result;
}

static PlcRuntimeLinkAccessV1 plc_link_access_for_definition(const PointDefinition& definition)
{
    switch (definition.direction) {
    case PointDirection::Output:
        return kPlcRuntimeLinkWrite;
    case PointDirection::InOut:
        return kPlcRuntimeLinkReadWrite;
    case PointDirection::Input:
    default:
        return kPlcRuntimeLinkRead;
    }
}

static PlcLinkerSelfTestResult plc_run_linker_self_test(const PointCatalog& catalog,
                                                        const PlcRuntimePublisherV1& publisher)
{
    PlcLinkerSelfTestResult result = {};
    result.resolve_ok = true;
    result.link_ok = false;
    result.link_result.status = kPlcObjectLinkInvalidArgument;
    result.link_result.resolve_status = kPlcRuntimeLinkResolved;

    const PointDefinition* definitions = catalog.entries();
    constexpr uint16_t kMaxSampleSymbols = 3u;
    PlcObjectSymbolRecordV1 symbols[kMaxSampleSymbols] = {};
    PlcObjectRelocationRecordV1 relocations[kMaxSampleSymbols] = {};
    uint16_t expected_indices[kMaxSampleSymbols] = {};
    uint8_t object_code[kMaxSampleSymbols * 2u] = {};
    uint8_t linked_code[kMaxSampleSymbols * 2u] = {};

    uint16_t sample_count = 0u;
    uint16_t skipped_points = 0u;
    for (size_t index = 0; index < catalog.size(); ++index) {
        const PointDefinition& definition = definitions[index];
        PlcRuntimePublisherV1::LinkRequest request = {};
        request.point_id = definition.id;
        request.expected_type = definition.value_type;
        request.access = plc_link_access_for_definition(definition);

        const PlcRuntimePublisherV1::LinkResult link_result = publisher.resolveLinkRequest(catalog, request);
        if (link_result.status == kPlcRuntimeLinkResolved) {
            ++result.resolved_points;
            if (sample_count < kMaxSampleSymbols) {
                symbols[sample_count].point_id = definition.id;
                symbols[sample_count].expected_type = static_cast<uint8_t>(definition.value_type);
                symbols[sample_count].access = static_cast<uint8_t>(request.access);
                relocations[sample_count].code_offset = static_cast<uint32_t>(sample_count * 2u);
                relocations[sample_count].symbol_index = sample_count;
                relocations[sample_count].relocation_kind = kPlcRelocationPointIndexU16Le;
                expected_indices[sample_count] = link_result.runtime_point_index;
                ++sample_count;
            }
            continue;
        }

        if (link_result.status == kPlcRuntimeLinkUnsupportedPointType) {
            ++skipped_points;
            continue;
        }

        result.resolve_ok = false;
        return result;
    }

    if (result.resolved_points != publisher.publishedCount() ||
        skipped_points != publisher.skippedCount() ||
        sample_count == 0u) {
        result.resolve_ok = false;
        return result;
    }

    PlcObjectImageV1 object_image = {};
    object_image.code_bytes = object_code;
    object_image.code_size = static_cast<uint32_t>(sample_count * 2u);
    object_image.entry_offset = 0u;
    object_image.symbols = symbols;
    object_image.symbol_count = sample_count;
    object_image.relocations = relocations;
    object_image.relocation_count = sample_count;

    result.link_result = PlcObjectLinkerV1::linkObjectImage(publisher,
                                                            catalog,
                                                            object_image,
                                                            linked_code,
                                                            sizeof(linked_code));
    if (result.link_result.status != kPlcObjectLinkOk) {
        return result;
    }

    for (uint16_t i = 0u; i < sample_count; ++i) {
        const uint16_t patched_index = static_cast<uint16_t>(linked_code[i * 2u]) |
                                       static_cast<uint16_t>(linked_code[i * 2u + 1u] << 8u);
        if (patched_index != expected_indices[i]) {
            result.link_result.status = kPlcObjectLinkResolveFailed;
            result.resolve_ok = false;
            return result;
        }
    }

    result.link_ok = true;
    result.sample_relocations = sample_count;
    return result;
}

static PlcLoaderSelfTestResult plc_run_loader_self_test(const PointCatalog& catalog,
                                                        const PlcRuntimePublisherV1& publisher,
                                                        bool include_flash_roundtrip)
{
    PlcLoaderSelfTestResult result = {};
    result.parse_ok = false;
    result.load_ok = false;
    result.package_ok = false;
    result.flash_ok = !include_flash_roundtrip;

    const PointDefinition* definitions = catalog.entries();
    constexpr uint16_t kMaxSampleSymbols = 3u;
    PlcObjectSymbolRecordV1 symbols[kMaxSampleSymbols] = {};
    PlcObjectRelocationRecordV1 relocations[kMaxSampleSymbols] = {};
    uint16_t expected_indices[kMaxSampleSymbols] = {};
    uint16_t sample_count = 0u;

    for (size_t index = 0; index < catalog.size() && sample_count < kMaxSampleSymbols; ++index) {
        const PointDefinition& definition = definitions[index];
        PlcRuntimePublisherV1::LinkRequest request = {};
        request.point_id = definition.id;
        request.expected_type = definition.value_type;
        request.access = plc_link_access_for_definition(definition);

        const PlcRuntimePublisherV1::LinkResult link_result = publisher.resolveLinkRequest(catalog, request);
        if (link_result.status != kPlcRuntimeLinkResolved) {
            continue;
        }

        symbols[sample_count].point_id = definition.id;
        symbols[sample_count].expected_type = static_cast<uint8_t>(definition.value_type);
        symbols[sample_count].access = static_cast<uint8_t>(request.access);
        relocations[sample_count].code_offset = static_cast<uint32_t>(sample_count * 2u);
        relocations[sample_count].symbol_index = sample_count;
        relocations[sample_count].relocation_kind = kPlcRelocationPointIndexU16Le;
        expected_indices[sample_count] = link_result.runtime_point_index;
        ++sample_count;
    }

    if (sample_count == 0u) {
        return result;
    }

    const size_t code_size = static_cast<size_t>(sample_count) * 2u;
    const size_t symbol_offset = sizeof(PlcObjectFileHeaderV1) + code_size;
    const size_t relocation_offset = symbol_offset + static_cast<size_t>(sample_count) * sizeof(PlcObjectSymbolRecordV1);
    const size_t object_size = relocation_offset + static_cast<size_t>(sample_count) * sizeof(PlcObjectRelocationRecordV1);
    uint8_t object_file[sizeof(PlcObjectFileHeaderV1) + (kMaxSampleSymbols * 2u) +
                        (kMaxSampleSymbols * sizeof(PlcObjectSymbolRecordV1)) +
                        (kMaxSampleSymbols * sizeof(PlcObjectRelocationRecordV1))] = {};

    PlcObjectFileHeaderV1 header = {};
    header.magic = kPlcObjectFileMagicV1;
    header.version = kPlcObjectFileVersionV1;
    header.code_size = static_cast<uint32_t>(code_size);
    header.entry_offset = 0u;
    header.symbol_count = sample_count;
    header.relocation_count = sample_count;
    header.symbol_table_offset = static_cast<uint32_t>(symbol_offset);
    header.relocation_table_offset = static_cast<uint32_t>(relocation_offset);
    std::memcpy(object_file, &header, sizeof(header));
    std::memcpy(object_file + symbol_offset, symbols, static_cast<size_t>(sample_count) * sizeof(PlcObjectSymbolRecordV1));
    std::memcpy(object_file + relocation_offset,
                relocations,
                static_cast<size_t>(sample_count) * sizeof(PlcObjectRelocationRecordV1));

    const PlcObjectParseResultV1 parse_result = PlcObjectLinkerV1::parseObjectFile(object_file, object_size);
    result.parse_ok = parse_result.status == kPlcObjectParseOk;
    result.sample_symbols = sample_count;
    if (!result.parse_ok) {
        result.load_result.parse_status = parse_result.status;
        result.load_result.status = kPlcSlotLoadParseFailed;
        return result;
    }

    result.load_result = PlcSlotLoaderV1::loadObjectFileIntoSlot(publisher,
                                                                 catalog,
                                                                 1u,
                                                                 object_file,
                                                                 object_size,
                                                                 200u,
                                                                 10000u);
    if (result.load_result.status != kPlcSlotLoadOk) {
        return result;
    }

    if (!plc_verify_loaded_slot(1u,
                                code_size,
                                sample_count,
                                expected_indices,
                                200u,
                                10000u,
                                publisher.storeEpoch())) {
        return result;
    }

    result.load_ok = true;

    uint8_t linked_code[kMaxSampleSymbols * 2u] = {};
    PlcObjectImageV1 object_image = {};
    object_image.code_bytes = object_file + sizeof(PlcObjectFileHeaderV1);
    object_image.code_size = static_cast<uint32_t>(code_size);
    object_image.entry_offset = 0u;
    object_image.symbols = symbols;
    object_image.symbol_count = sample_count;
    object_image.relocations = relocations;
    object_image.relocation_count = sample_count;

    const PlcObjectLinkResultV1 package_link_result = PlcObjectLinkerV1::linkObjectImage(publisher,
                                                                                          catalog,
                                                                                          object_image,
                                                                                          linked_code,
                                                                                          sizeof(linked_code));
    if (package_link_result.status != kPlcObjectLinkOk) {
        result.package_load_result.link_result = package_link_result;
        return result;
    }

    uint8_t package_bytes[sizeof(PlcLinkedProgramPackageHeaderV1) + (kMaxSampleSymbols * 2u)] = {};
    PlcLinkedProgramPackageHeaderV1 package_header = {};
    package_header.magic = kPlcLinkedProgramPackageMagicV1;
    package_header.version = kPlcRuntimeAbiV1Version;
    package_header.abi_version = kPlcRuntimeAbiV1Version;
    package_header.code_size = static_cast<uint32_t>(code_size);
    package_header.entry_offset = 0u;
    package_header.symbol_count = sample_count;
    package_header.relocation_count = sample_count;
    package_header.max_instructions_per_scan = 300u;
    package_header.max_scan_time_us = 12000u;
    package_header.runtime_header_addr = kPlcRuntimeHeaderAddr;
    package_header.store_epoch = publisher.storeEpoch();
    package_header.linked_code_checksum = 2166136261u;
    for (size_t i = 0u; i < code_size; ++i) {
        package_header.linked_code_checksum ^= linked_code[i];
        package_header.linked_code_checksum *= 16777619u;
    }
    std::memcpy(package_bytes, &package_header, sizeof(package_header));
    std::memcpy(package_bytes + sizeof(package_header), linked_code, code_size);

    result.package_load_result = PlcSlotLoaderV1::loadLinkedProgramPackageIntoSlot(publisher,
                                                                                    2u,
                                                                                    package_bytes,
                                                                                    sizeof(package_header) + code_size);
    if (result.package_load_result.status != kPlcSlotLoadOk) {
        return result;
    }
    if (!plc_verify_loaded_slot(2u,
                                code_size,
                                sample_count,
                                expected_indices,
                                300u,
                                12000u,
                                publisher.storeEpoch())) {
        return result;
    }

    result.package_ok = true;

    if (include_flash_roundtrip) {
        Flash flash(FLASH0_BASE);
        const size_t flash_package_size = sizeof(PlcLinkedProgramPackageHeaderV1) + code_size;
        const uint32_t flash_erase_size = static_cast<uint32_t>(((flash_package_size + Flash::kSectorSize - 1u) /
                                                                 Flash::kSectorSize) * Flash::kSectorSize);
        if (flash_package_size > Flash::kPlcPackageSlotSize ||
            !plc_flash_erase_range(flash, Flash::kPlcPackageSlotBase, flash_erase_size) ||
            !plc_flash_write_erased_bytes(flash,
                                          Flash::kPlcPackageSlotBase,
                                          package_bytes,
                                          flash_package_size)) {
            result.flash_load_result.status = kPlcSlotLoadFlashReadFailed;
            return result;
        }

        result.flash_load_result = PlcSlotLoaderV1::loadLinkedProgramPackageFromFlash(publisher,
                                                                                       flash,
                                                                                       3u,
                                                                                       Flash::kPlcPackageSlotBase,
                                                                                       Flash::kPlcPackageSlotSize);
        if (result.flash_load_result.status != kPlcSlotLoadOk) {
            return result;
        }
        if (!plc_verify_loaded_slot(3u,
                                    code_size,
                                    sample_count,
                                    expected_indices,
                                    300u,
                                    12000u,
                                    publisher.storeEpoch())) {
            return result;
        }

        result.flash_ok = true;
    }

    return result;
}

int main(void)
{
    // Initial startup LED blink to indicate booting.
    led_d2_blink();
    // Harware definition
    WbLed  ledGreen(LED0_BASE);
    WbLed  ledYellow(LED1_BASE);    

    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    const uint32_t sdram_probe_addr0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_sdram_test_scratch_words[0]));
    const uint32_t sdram_probe_addr1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_sdram_test_scratch_words[1]));
    WbSdramTestMaster sdramMasterTest;
    SdramFwTestContext sdramFwTest = {};
    sdramFwTest.stage = SdramFwTestStage::Idle;
    sdramFwTest.fail_stage = SdramFwTestStage::Idle;
    sdramFwTest.next_oled_refresh_ms = millis();
    g_oled_slot_refresh_deadline_ms = millis();

    *LED_D2 = 1u;
    oled_init(0x3C);
    oled_write("  NodeNet SoC RISC-V");
    oled_write("v" FIRMWARE_VERSION);
    if (kEnableSdramBringupTest) {
        sdramMasterTest.stop();
        sdramMasterTest.clearCounters();
        oled_write("[SDRAM] FW test arm");
    }

    // NodeNet definition and initialization
    static NodeNet myNodeNet(
        NODENET0_BASE,
        0x04,
        1000000,
        200,
        nullptr,
        nullptr);


    static NodeNetCore nodeNetCore(&myNodeNet);
    nodeNetCore.begin();

    PlcRuntimePublisherV1 plcRuntimePublisher;
    const bool plcRuntimeAbiReady = plcRuntimePublisher.begin();
    if (plcRuntimeAbiReady) {
        (void)plcRuntimePublisher.publish(nodeNetCore.pointCatalog(), millis());
        nodeNetCore.attachPlcRuntimePublisher(&plcRuntimePublisher);
        oled_configure_plc_slot_icon(0u, 0u, PlcSlotIconSource::None);
        const PlcRuntimeHeaderV1 plcRuntimeHeader = plcRuntimePublisher.headerSnapshot();
        oled_print("[PLC] ABI %u/%u e%lu",
                   static_cast<unsigned>(plcRuntimeHeader.descriptor_count),
                   static_cast<unsigned>(plcRuntimePublisher.skippedCount()),
                   static_cast<unsigned long>(plcRuntimeHeader.store_epoch));
        if (kEnablePlcLinkerSelfTest) {
            const PlcLinkerSelfTestResult link_test =
                plc_run_linker_self_test(nodeNetCore.pointCatalog(), plcRuntimePublisher);
            if (link_test.resolve_ok && link_test.link_ok) {
                oled_print("[PLC] LINK %u r/%u ok",
                           static_cast<unsigned>(link_test.resolved_points),
                           static_cast<unsigned>(link_test.sample_relocations));
                const PlcLoaderSelfTestResult load_test =
                    plc_run_loader_self_test(nodeNetCore.pointCatalog(),
                                             plcRuntimePublisher,
                                             kEnablePlcFlashRoundTripSelfTest);
                if (load_test.parse_ok && load_test.load_ok && load_test.package_ok && load_test.flash_ok) {
                    oled_print(kEnablePlcFlashRoundTripSelfTest ? "[PLC] SLOT1-3 %u ok"
                                                                : "[PLC] SLOT1-2 %u ok",
                               static_cast<unsigned>(load_test.sample_symbols));
                } else {
                    oled_print("[PLC] SLOT %u/%u/%u",
                               static_cast<unsigned>(load_test.load_result.status),
                               static_cast<unsigned>(load_test.package_load_result.status),
                               static_cast<unsigned>(load_test.flash_load_result.status));
                }
            } else {
                oled_print("[PLC] LNK fail s%u r%u",
                           static_cast<unsigned>(link_test.link_result.status),
                           static_cast<unsigned>(link_test.link_result.resolve_status));
            }
        }

        Flash plcFlash(FLASH0_BASE);
        const PlcSlot0BootResult slot0_boot = plc_boot_slot0_program(nodeNetCore,
                                                                     nodeNetCore.pointCatalog(),
                                                                     plcRuntimePublisher,
                                                                     plcFlash,
                                                                     1u);
        g_oled_slot_icons[0].source = (slot0_boot.source == PlcSlot0BootSource::FlashPackage)
                                          ? PlcSlotIconSource::Flash
                                          : ((slot0_boot.source == PlcSlot0BootSource::HardcodedMirror)
                                                 ? PlcSlotIconSource::Local
                                                 : PlcSlotIconSource::None);
        (void)oled_refresh_plc_slot_icon(0u);
        if (slot0_boot.load_ok) {
            if (slot0_boot.source == PlcSlot0BootSource::FlashPackage) {
                oled_print("[PLC] VM S0 flash ok");
            } else {
                oled_print("[PLC] VM S0 in%u>out%u",
                           static_cast<unsigned>(slot0_boot.channel),
                           static_cast<unsigned>(slot0_boot.channel));
            }
        } else {
            oled_print("[PLC] VM S0 err %u/%u",
                       static_cast<unsigned>(slot0_boot.flash_load_result.status),
                       static_cast<unsigned>(slot0_boot.fallback_load_result.status));
        }
    } else {
        oled_write("[PLC] ABI region bad");
    }
    // oled_write("[NN] ctor ok");
    // const bool nodenet_ok = myNodeNet.test(oled_boot_status);

    // // POST Tests
    // oled_write("[BOOT] Running tests");
    // oled_write(nodenet_ok ? "[NN] Self-test PASS" : "[NN] Self-test FAIL");

    // uint8_t modbus_channel = 1u;
    // uint8_t modbus_slave_addr = MODBUS1_SLAVE_ADDR_DEFAULT;
    // bool modbus_channel_state[8] = {false, false, false, false, false, false, false, false};

    // const bool sdram_ok = sdramTest(oled_boot_status);
    // oled_write(sdram_ok ? "[BOOT] System ready" : "[BOOT] Degraded mode");

    // const bool flash_ok = myFlash.lowLevelTest(oled_boot_status);
    // oled_write(flash_ok ? "[FLASH] LowLevel PASS" : "[FLASH] LowLevel FAIL");

    // bool flashdb_ok = false;
    // if (flash_ok) {
    //     flashdb_ok = flashdb_init(&myFlash, oled_boot_status);
    //     oled_write(flashdb_ok ? "[FDB] Ready" : "[FDB] Init FAIL");
    // } else {
    //     oled_write("[FDB] skip (flash)");
    // }
    // if (flashdb_ok) {
    //     (void)flashdb_boot_counter_test(oled_boot_status);
    // }

    // // Probe Waveshare serial settings using slave=0 discovery command.
    // bool modbus_found = false;
    // uint16_t device_addr_reg = 0u;
    // uint16_t version_reg = 0u;
    // static const uint32_t kProbeBaud[] = {9600u, 4800u, 19200u, 38400u, 57600u, 115200u, 128000u, 256000u};
    // for (uint32_t baud : kProbeBaud) {
    //     modbus1.begin(baud, 200u, 0u);
    //     modbus1.setInterframeCharsQ1(14u);
    //     // Waveshare extension: slave 0 query for 0x4000 returns actual device address.
    //     if (modbus1.readHoldingRegisters(0u, 0x4000u, 1u, &device_addr_reg)) {
    //         uint8_t detected = static_cast<uint8_t>(device_addr_reg & 0x00FFu);
    //         if (detected == 0u) {
    //             detected = MODBUS1_SLAVE_ADDR_DEFAULT;
    //         }
    //         modbus_slave_addr = detected;
    //         modbus_found = true;
    //         oled_print("[MB] link @%lu", static_cast<unsigned long>(baud));
    //         oled_print("[MB] slave=%u", static_cast<unsigned>(modbus_slave_addr));
    //         break;
    //     }
    // }

    // // Fallback: small direct slave scan on the default baud.
    // if (!modbus_found) {
    //     modbus1.begin(9600u, 250u, 1u);
    //     modbus1.setInterframeCharsQ1(14u);
    //     for (uint8_t slave = 1u; slave <= 16u; ++slave) {
    //         if (modbus1.readHoldingRegisters(slave, 0x4000u, 1u, &device_addr_reg)) {
    //             modbus_slave_addr = slave;
    //             modbus_found = true;
    //             oled_print("[MB] fallback @9600");
    //             oled_print("[MB] slave=%u", static_cast<unsigned>(modbus_slave_addr));
    //             break;
    //         }
    //     }
    // }

    // if (!modbus_found) {
    //     modbus1.begin(9600u, 500u, 2u);
    //     modbus1.setInterframeCharsQ1(14u);
    //     oled_write("[MB] probe failed");
    // } else if (modbus1.readHoldingRegisters(modbus_slave_addr, 0x8000u, 1u, &version_reg)) {
    //     char version_text[16] = {};
    //     format_version_hundredths(version_reg, version_text, sizeof(version_text));
    //     oled_print("[MB] ver=0x%04X", static_cast<unsigned>(version_reg));
    //     oled_write(version_text);
    // } else {
    //     char status_hex[11] = {};
    //     hex32_to_str(modbus1.lastHwStatus(), status_hex);
    //     oled_write("[MB] ver read fail");
    //     oled_write(status_hex);
    // }

    // char device_id[12] = {};
    // if (myFlash.readUniqueIdAscii(device_id, sizeof(device_id))) {
    //     oled_print("[FLASH] ID %s", device_id);
    // } else {
    //     oled_write("[FLASH] ID read fail");
    // }

    // if (!sdram_json_allocator_init()) {
    //     oled_write("[JSON] alloc init fail");
    // } else {
    //     JsonDocument doc(&g_sdram_json_allocator);
    //     doc["test"] = 123;
    //     char buffer[128] = {};
    //     const size_t n = serializeJson(doc, buffer, sizeof(buffer));
    //     if (n > 0u && n < sizeof(buffer)) {
    //         oled_print("[JSON] %s", buffer);
    //     } else {
    //         oled_write("[JSON] serialize fail");
    //     }
    // }

    // myNodeNet.SetCallbacks(nodenet_broadcast_callback, nodenet_message_callback);
    // oled_write("[NN] irq armed");

    while (1) {
        nodeNetCore.loop();
        oled_refresh_plc_slot_icons_if_due(millis());
        if (plcRuntimeAbiReady) {
            (void)plcRuntimePublisher.publishIfDue(nodeNetCore.pointCatalog(), millis());
        }
        if (kEnableSdramBringupTest) {
            sdram_update_test(&sdramFwTest,
                              sdramMasterTest,
                              sdram_probe_addr0,
                              sdram_probe_addr1,
                              millis(),
                              ledGreen,
                              ledYellow);
        }
        //nodenet_process_pending_events();
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            // const uint16_t coil_addr = static_cast<uint16_t>(modbus_channel - 1u);
            // const uint8_t idx = static_cast<uint8_t>(modbus_channel - 1u);
            // modbus_channel_state[idx] = !modbus_channel_state[idx];
            // const bool ok = modbus1.writeSingleCoil(modbus_slave_addr, coil_addr, modbus_channel_state[idx]);
            // if (!ok) {
            //     char status_hex[11] = {};
            //     hex32_to_str(modbus1.lastHwStatus(), status_hex);
            //     oled_print("[MB] CH%u err=%u", modbus_channel, static_cast<unsigned>(modbus1.lastError()));
            //     oled_write(status_hex);
            //     ledYellow.blink(250u);
            // } else {
            //     ledGreen.blink(100u);
            // }

            // modbus_channel = static_cast<uint8_t>(modbus_channel + 1u);
            // if (modbus_channel > 8u) {
            //     modbus_channel = 1u;
            // }

            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
        }
    }
}