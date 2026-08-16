#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include "bigsister.h"
#include "led.h"
#include "i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"
#include "version.h"
#include "sdram.h"
#include "nodenet.h"
#include "Serial.h"
#include "flash.h"
#include "flashdb_port.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL
#define SERIAL1_BASE 0x10004000u
#define FLASH_BASE 0x10007000u
static constexpr uint32_t NODENET0_BASE = 0x10006000u;

#define SERIAL1_BUFFER_LENGTH 32

// ════════════════════════════════════════════════════════════════════════════
// OLED Display (U8G2)
// ════════════════════════════════════════════════════════════════════════════
static u8g2_t g_oled;
static bool g_oled_ready = false;
static constexpr bool kOledRenderBypass = true;  // Temporary debug gate.
static constexpr bool kOledInitBypass = true;    // Temporary debug gate.
static constexpr uint8_t kOledConsoleLines = 5;
static constexpr uint8_t kOledConsoleCols = 21;
static char g_oled_console[kOledConsoleLines][kOledConsoleCols + 1] = {};
static uint8_t g_oled_console_count = 0;

static void dbg_stage(uint8_t count);

static bool oled_init(uint8_t addr7 = 0x3C)
{
    dbg_stage(21);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&g_oled, U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
    dbg_stage(22);

    u8g2_SetI2CAddress(&g_oled, static_cast<uint8_t>(addr7 << 1));
    dbg_stage(23);

    u8g2_InitDisplay(&g_oled);
    dbg_stage(24);

    u8g2_SetPowerSave(&g_oled, 0);
    dbg_stage(25);

    u8g2_ClearBuffer(&g_oled);
    dbg_stage(26);

    u8g2_SendBuffer(&g_oled);
    dbg_stage(27);

    g_oled_ready = true;
    return true;
}
// Console-style OLED write: append one line and scroll when full.
static void oled_write(const char* text)
{
    static bool first_oled_trace = true;

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

    if (kOledRenderBypass) {
        if (first_oled_trace) {
            dbg_stage(11);
            first_oled_trace = false;
        }
        return;
    }

    if (first_oled_trace) dbg_stage(11);
    u8g2_SetFont(&g_oled, u8g2_font_6x12_tf);
    if (first_oled_trace) dbg_stage(12);

    u8g2_ClearBuffer(&g_oled);
    if (first_oled_trace) dbg_stage(13);

    for (uint8_t row = 0; row < g_oled_console_count; ++row) {
        u8g2_DrawStr(&g_oled, 2, 10 + row * 13, g_oled_console[row]);
    }
    if (first_oled_trace) dbg_stage(14);

    u8g2_SendBuffer(&g_oled);
    if (first_oled_trace) {
        dbg_stage(15);
        first_oled_trace = false;
    }

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

static void led_d2_blink()
{
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    delay(500u);
}

static void dbg_stage(uint8_t count)
{
    auto raw_delay = [](uint32_t cycles) {
        for (volatile uint32_t i = 0; i < cycles; ++i) {
            __asm__ volatile("nop" ::: "memory");
        }
    };

    // Distinct startup stage marker: N short pulses then a long separator.
    for (uint8_t i = 0; i < count; ++i) {
        *LED_D2 = 1u;
        raw_delay(450000u);
        *LED_D2 = 0u;
        raw_delay(450000u);
    }
    raw_delay(1800000u);
}

int main(void)
{
    dbg_stage(1); // entered minimal firmware main

    auto raw_delay = [](uint32_t cycles) {
        for (volatile uint32_t i = 0; i < cycles; ++i) {
            __asm__ volatile("nop" ::: "memory");
        }
    };

    while (1) {
        // Isolated heartbeat: no LED0/LED1 access.
        *LED_D2 = 1u;
        raw_delay(4000000u);

        *LED_D2 = 0u;
        raw_delay(4000000u);
    }
}