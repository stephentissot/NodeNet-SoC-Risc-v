#include <cstdint>
#include "bigsister.h"
#include "sdram.h"
#include "led.h"
#include "i2c0.h"
#include "u8g2.h"
#include "u8g2_hal.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL



// ─── OLED ────────────────────────────────────────────────────────────────────
static volatile u8g2_t u8g2;

static inline u8g2_t *u8g2_access() {
    return const_cast<u8g2_t *>(&u8g2);
}

static void oled_init() {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_access(), U8G2_R0,
                                            u8x8_byte_i2c_hw,
                                            u8x8_gpio_delay_hw);
    u8g2_SetI2CAddress(u8g2_access(), 0x3C << 1);  // explicit 0x3C (7-bit)
    u8g2_InitDisplay(u8g2_access());
    u8g2_SetPowerSave(u8g2_access(), 0);
    u8g2_SetFont(u8g2_access(), u8g2_font_5x7_tf);
}

static void oled_show(const char *line0, const char *line1 = nullptr,
                      const char *line2 = nullptr) {
    u8g2_ClearBuffer(u8g2_access());
    u8g2_DrawStr(u8g2_access(), 0, 10, line0);
    if (line1) u8g2_DrawStr(u8g2_access(), 0, 20, line1);
    if (line2) u8g2_DrawStr(u8g2_access(), 0, 30, line2);
    u8g2_SendBuffer(u8g2_access());
}

static void delay(uint32_t ms) {
    uint32_t start = millis();
    while ((int32_t)(millis() - start - ms) < 0) {}
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
    //sdram_wait_ready();
    WbLed  led0(LED0_BASE);
    WbLed  led1(LED1_BASE);
    bool s_oled_ok = false;
    bool s_oled_init = false;
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;

    i2c0_init(15); // 400 kHz @ 25 MHz
    
    while (1) {
        if(!s_oled_ok) s_oled_ok = i2c0_probe(0x3C); // Try i2c address 0x3C (OLED)
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            if (!s_oled_ok) led1.blink(600u);
            
            if(s_oled_ok && !s_oled_init) {
                // Diagnostic blinks: 1=before Setup, 2=before InitDisplay, 3=before PowerSave, 4=before Font → done                
                for(int i=0;i<5;++i){ led0.blink(150u); delay(450u); }
                u8g2_SetI2CAddress(u8g2_access(), 0x3C << 1);

                // Diagnostic: exercise the len>1 write path directly.
                const uint8_t len2_test_bytes[2] = {0x00u, 0x00u};
                const int len2_result = i2c0_write(0x3C, len2_test_bytes, sizeof(len2_test_bytes));
                if (len2_result != I2C0_OK) {
                    for (int i = 0; i < 2; ++i) { led1.blink(150u); delay(450u); }
                } else {
                    for (int i = 0; i < 1; ++i) { led0.blink(150u); delay(450u); }
                }
                //Second test
                const uint8_t len3_test_bytes[3] = {0x00u, 0x00u, 0x00u};
                const int len3_result = i2c0_write(0x3C, len3_test_bytes, sizeof(len3_test_bytes));
                if (len3_result != I2C0_OK) {
                    for (int i = 0; i < 2; ++i) { led1.blink(150u); delay(450u); }
                } else {
                    for (int i = 0; i < 1; ++i) { led0.blink(150u); delay(450u); }
                }
                
                // bool oledTest = oled_status_read_test(0x3C);
                // if(!oledTest) {
                //     for(int i=0;i<3;++i){ led1.blink(150u); delay(450u); }
                // } else {
                //     for(int i=0;i<2;++i){ led0.blink(150u); delay(450u); }
                // }

                // for(int i=0;i<4;++i){ led0.blink(150u); delay(450u); }
                // u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_access(), U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
                // for(int i=0;i<3;++i){ led0.blink(150u); delay(450u); }
                // u8g2_InitDisplay(u8g2_access());
                // for(int i=0;i<2;++i){ led0.blink(150u); delay(450u); }
                // u8g2_SetPowerSave(u8g2_access(), 0);

                // for(int i=0;i<1;++i){ led0.blink(150u); delay(450u); }
                // u8g2_SetFont(u8g2_access(), u8g2_font_5x7_tf);
                // s_oled_init = true;
            }
            if(s_oled_ok && s_oled_init) {
                oled_show("I2C OK", "OLED found at 0x3C");
            }
        }
    }
}