#include <cstdint>
#include "bigsister.h"
#include "led.h"
#include "i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL
// ════════════════════════════════════════════════════════════════════════════
// OLED Display (U8G2)
// ════════════════════════════════════════════════════════════════════════════
static u8g2_t g_oled;
static bool g_oled_ready = false;

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
// Draw text on OLED display at line n (0 is top)
static void oled_write(const uint8_t line,const char* text)
{
    if (!g_oled_ready || text == nullptr) {
        return;
    }

    u8g2_SetFont(&g_oled, u8g2_font_6x12_tf);
    u8g2_DrawStr(&g_oled, 1, 10 + line * 14, text);

}

static void led_d2_blink()
{
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    delay(500u);
}

// Tests
static const uint8_t seq_on[] = {0x00, 0xA4};
static const uint8_t seq_off[] = {0x00, 0xA5};
static const uint8_t seq_screen_off[] = {0x00, 0xAE};
static const uint8_t seq_screen_on[] = {0x00, 0xAF};
// Init screen
static const uint8_t seq_init_screen[] = {
        0x00,       // Control byte: following bytes are commands

        0xAE,       // Display OFF

        0xD5, 0x80, // Set display clock divide ratio / oscillator frequency
                    // 0x80 = default recommended setting

        0xA8, 0x3F, // Set multiplex ratio
                    // 0x3F = 63 -> 64 MUX for a 128x64 panel

        0xD3, 0x00, // Set display offset
                    // 0x00 = no vertical offset

        0x40,       // Set display start line to 0

        0x8D, 0x14, // Enable charge pump
                    // 0x14 = internal charge pump ON

        0xA1,       // Segment remap
                    // Column address 127 is mapped to SEG0
                    // Common on many OLED modules depending on orientation

        0xC8,       // COM output scan direction remapped
                    // Flips vertical scan direction

        0xDA, 0x12, // Set COM pins hardware configuration
                    // 0x12 is the usual setting for 128x64 SSD1306

        0x81, 0xCF, // Set contrast control
                    // 0xCF = common default contrast value

        0xD9, 0xF1, // Set pre-charge period
                    // 0xF1 is typical when using internal charge pump

        0xDB, 0x40, // Set VCOMH deselect level
                    // 0x40 is a common default

        0xA4,       // Resume display from RAM content
                    // Opposite of "entire display ON" (0xA5)

        0xA6,       // Normal display mode
                    // Opposite of inverse display (0xA7)

        0xAF        // Display ON
    };



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
    u8g2_ClearBuffer(&g_oled);
    oled_write(0,"  NodeNet SoC RISC-V");
    oled_write(1,"     V 0.0.1 Beta");
    oled_write(3," (c) BigSister 2026");
    
    u8g2_SendBuffer(&g_oled);
    while (1) {
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            ledGreen.blink(100u);            
        }
    }
}