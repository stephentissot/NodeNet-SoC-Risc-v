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

int main(void)
{
    // Initial startup LED blink to indicate booting.
    led_d2_blink();
    // Harware definition
    WbLed  ledGreen(LED0_BASE);
    WbLed  ledYellow(LED1_BASE);
    Serial Serial1(SERIAL1_BASE);
    Serial1.begin(115200u);
    Flash myFlash(FLASH_BASE);

    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;
    oled_init(0x3C);
    oled_write("  NodeNet SoC RISC-V");
    oled_write("v" FIRMWARE_VERSION);

    // NodeNet definition and initialization
    NodeNet myNodeNet(NODENET0_BASE, 0x41, 1000000, NODENET_PRIORITY_NORMAL, 200);
    const bool nodenet_ok = myNodeNet.test(oled_boot_status);

    // POST Tests
    oled_write("[BOOT] Running tests");
    oled_write(nodenet_ok ? "[NN] Self-test PASS" : "[NN] Self-test FAIL");

    // Serial definition and initialization

    char serial1rxBuffer[SERIAL1_BUFFER_LENGTH] = {};
    uint8_t serial1rxCount = 0;

    const bool sdram_ok = sdramTest(oled_boot_status);
    oled_write(sdram_ok ? "[BOOT] System ready" : "[BOOT] Degraded mode");

    const bool flash_ok = myFlash.lowLevelTest(oled_boot_status);
    oled_write(flash_ok ? "[FLASH] LowLevel PASS" : "[FLASH] LowLevel FAIL");

    bool flashdb_ok = false;
    if (flash_ok) {
        flashdb_ok = flashdb_init(&myFlash, oled_boot_status);
        oled_write(flashdb_ok ? "[FDB] Ready" : "[FDB] Init FAIL");
    } else {
        oled_write("[FDB] skip (flash)");
    }
    if (flashdb_ok) {
        (void)flashdb_boot_counter_test(oled_boot_status);
    }

    while (1) {

        // Check serial1 input and echo back any received characters
        while (Serial1.available()) {
            uint8_t c = Serial1.read();
            if (static_cast<char>(c) == '\n') {
                oled_write("[RX1] Received:");
                oled_write(serial1rxBuffer);
                serial1rxCount = 0;
                serial1rxBuffer[0] = '\0';
            }
            else if (serial1rxCount < SERIAL1_BUFFER_LENGTH - 1) {
                serial1rxBuffer[serial1rxCount++] = static_cast<char>(c);
                serial1rxBuffer[serial1rxCount] = '\0';
            }
        }

        if (myNodeNet.HasMessage()) {
            NodeNetMessage msg = myNodeNet.ReadMessage();
            // if(msg.src_addr != 0){ // No response to broadcast messages
            //     oled_print_rx_header(msg.src_addr, msg.len);
            //     oled_write_payload_safe(msg.data, msg.len);
            //     ledYellow.blink(300u);
            //     // Send a reply to the sender, then release RX buffer.
            //     myNodeNet.Send(msg.src_addr, "Hello from NodeNet!");
            // }

            NodeNet::FreeMessage(msg);
        }
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