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
#include "ModbusMaster.h"
#include "flash.h"
#include "flashdb_port.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL
#define MODBUS1_BASE 0x10004000u
#define FLASH_BASE 0x10007000u
static constexpr uint32_t NODENET0_BASE = 0x10006000u;
static constexpr uint8_t MODBUS1_SLAVE_ADDR_DEFAULT = 0x01u;

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
    ModbusMaster modbus1(MODBUS1_BASE);
    modbus1.begin(9600u, 500u, 2u);
    modbus1.setInterframeCharsQ1(14u);
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

    uint8_t modbus_channel = 1u;
    uint8_t modbus_slave_addr = MODBUS1_SLAVE_ADDR_DEFAULT;
    bool modbus_channel_state[8] = {false, false, false, false, false, false, false, false};

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

    // Probe Waveshare serial settings using slave=0 discovery command.
    bool modbus_found = false;
    uint16_t device_addr_reg = 0u;
    uint16_t version_reg = 0u;
    static const uint32_t kProbeBaud[] = {9600u, 4800u, 19200u, 38400u, 57600u, 115200u, 128000u, 256000u};
    for (uint32_t baud : kProbeBaud) {
        modbus1.begin(baud, 200u, 0u);
        modbus1.setInterframeCharsQ1(14u);
        // Waveshare extension: slave 0 query for 0x4000 returns actual device address.
        if (modbus1.readHoldingRegisters(0u, 0x4000u, 1u, &device_addr_reg)) {
            uint8_t detected = static_cast<uint8_t>(device_addr_reg & 0x00FFu);
            if (detected == 0u) {
                detected = MODBUS1_SLAVE_ADDR_DEFAULT;
            }
            modbus_slave_addr = detected;
            modbus_found = true;
            oled_print("[MB] link @%lu", static_cast<unsigned long>(baud));
            oled_print("[MB] slave=%u", static_cast<unsigned>(modbus_slave_addr));
            break;
        }
    }

    // Fallback: small direct slave scan on the default baud.
    if (!modbus_found) {
        modbus1.begin(9600u, 250u, 1u);
        modbus1.setInterframeCharsQ1(14u);
        for (uint8_t slave = 1u; slave <= 16u; ++slave) {
            if (modbus1.readHoldingRegisters(slave, 0x4000u, 1u, &device_addr_reg)) {
                modbus_slave_addr = slave;
                modbus_found = true;
                oled_print("[MB] fallback @9600");
                oled_print("[MB] slave=%u", static_cast<unsigned>(modbus_slave_addr));
                break;
            }
        }
    }

    if (!modbus_found) {
        modbus1.begin(9600u, 500u, 2u);
        modbus1.setInterframeCharsQ1(14u);
        oled_write("[MB] probe failed");
    } else if (modbus1.readHoldingRegisters(modbus_slave_addr, 0x8000u, 1u, &version_reg)) {
        char version_text[16] = {};
        format_version_hundredths(version_reg, version_text, sizeof(version_text));
        oled_print("[MB] ver=0x%04X", static_cast<unsigned>(version_reg));
        oled_write(version_text);
    } else {
        char status_hex[11] = {};
        hex32_to_str(modbus1.lastHwStatus(), status_hex);
        oled_write("[MB] ver read fail");
        oled_write(status_hex);
    }

    while (1) {

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
            const uint16_t coil_addr = static_cast<uint16_t>(modbus_channel - 1u);
            const uint8_t idx = static_cast<uint8_t>(modbus_channel - 1u);
            modbus_channel_state[idx] = !modbus_channel_state[idx];
            const bool ok = modbus1.writeSingleCoil(modbus_slave_addr, coil_addr, modbus_channel_state[idx]);
            if (!ok) {
                char status_hex[11] = {};
                hex32_to_str(modbus1.lastHwStatus(), status_hex);
                oled_print("[MB] CH%u err=%u", modbus_channel, static_cast<unsigned>(modbus1.lastError()));
                oled_write(status_hex);
                ledYellow.blink(250u);
            } else {
                ledGreen.blink(100u);
            }

            modbus_channel = static_cast<uint8_t>(modbus_channel + 1u);
            if (modbus_channel > 8u) {
                modbus_channel = 1u;
            }

            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
        }
    }
}