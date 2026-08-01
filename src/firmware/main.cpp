#include <cstdint>
#include "bigsister.h"
#include "led.h"
#include "i2c.h"

// Forward-declare HAL bus init — avoids pulling in u8g2.h here; full
// declaration in u8g2_hal.h.  Shares i2c0 with u8g2 callbacks (P1/P2).
void u8g2_hal_set_i2c(I2C* bus);

// Hardware setup
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED1_BASE 0x10000008UL
#define I2C0_BASE 0x10005000u

// ─── OLED ────────────────────────────────────────────────────────────────────
// static u8g2_t u8g2;
// static bool s_oled_ok = false;

// static void oled_init() {
//     u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
//                                             u8x8_byte_i2c_hw,
//                                             u8x8_gpio_delay_hw);
//     u8g2_SetI2CAddress(&u8g2, 0x3C << 1);  // explicit 0x3C (7-bit)
//     u8g2_InitDisplay(&u8g2);
//     u8g2_SetPowerSave(&u8g2, 0);
//     u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
// }

// static void oled_show(const char *line0, const char *line1 = nullptr,
//                       const char *line2 = nullptr) {
//     u8g2_ClearBuffer(&u8g2);
//     u8g2_DrawStr(&u8g2, 0, 10, line0);
//     if (line1) u8g2_DrawStr(&u8g2, 0, 20, line1);
//     if (line2) u8g2_DrawStr(&u8g2, 0, 30, line2);
//     u8g2_SendBuffer(&u8g2);
// }

int main(void)
{
    I2C    i2c0(I2C0_BASE);
    WbLed  led1(LED1_BASE);
    bool s_oled_ok = false;
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;

    i2c0.begin(); // 400 kHz @ 25 MHz
    u8g2_hal_set_i2c(&i2c0); // share single I2C instance with u8g2 HAL (P1)
    while (1) {
        i2c0.beginTransmission(0x3C);
        i2c0.write(0x00u);
        s_oled_ok = (i2c0.endTransmission() == 0);
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;

            if (!s_oled_ok) led1.blink(600u);
            else led1.blink(100u);
        }
    }
}