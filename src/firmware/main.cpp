#include <cstdint>
#include "bigsister.h"
#include "led.h"
#include "i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"
#include "version.h"
#include "sdram.h"
#include "nodenet.h"
#include "serial.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL
#define SERIAL1_BASE 0x10004000u
static constexpr uint32_t NODENET0_BASE = 0x10006000u;
// ════════════════════════════════════════════════════════════════════════════
// OLED Display (U8G2)
// ════════════════════════════════════════════════════════════════════════════
static u8g2_t g_oled;
static bool g_oled_ready = false;
static constexpr uint8_t kOledConsoleLines = 5;
static constexpr uint8_t kOledConsoleCols = 21;
static char g_oled_console[kOledConsoleLines][kOledConsoleCols + 1] = {};
static uint8_t g_oled_console_count = 0;

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
    u8g2_SendBuffer(&g_oled);

}

static void oled_boot_status(uint8_t line, const char* text)
{
    (void)line;
    oled_write(text);
}

static void led_d2_blink()
{
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    delay(500u);
}

int main(void)
{
    led_d2_blink();
    WbLed  ledGreen(LED0_BASE);
    WbLed  ledYellow(LED1_BASE);
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;
    oled_init(0x3C);
    oled_write("  NodeNet SoC RISC-V");
    oled_write("v" FIRMWARE_VERSION);
    oled_write("[BOOT] Running tests");
    //NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
    Serial Serial1(SERIAL1_BASE);
    Serial1.begin(115200u);
    const bool sdram_ok = sdramTest(oled_boot_status);
    oled_write(sdram_ok ? "[BOOT] System ready" : "[BOOT] Degraded mode");
    
    while (1) {
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            Serial1.println("Hello");
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            ledGreen.blink(100u);            
        }
    }
}