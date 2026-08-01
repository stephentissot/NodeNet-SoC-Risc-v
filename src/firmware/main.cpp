#include <cstdint>
#include "bigsister.h"
#include "sdram.h"
#include "led.h"
#include "i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
/* I2C0_BASE défini dans i2c.h */


// ─── OLED ────────────────────────────────────────────────────────────────────
static u8g2_t u8g2;


static void oled_init() {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
                                            u8x8_byte_i2c_hw,
                                            u8x8_gpio_delay_hw);
    u8g2_SetI2CAddress(&u8g2, 0x3C << 1);  // explicit 0x3C (7-bit)make
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
}

static void oled_show(const char *line0, const char *line1 = nullptr,
                      const char *line2 = nullptr) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawStr(&u8g2, 0, 10, line0);
    if (line1) u8g2_DrawStr(&u8g2, 0, 20, line1);
    if (line2) u8g2_DrawStr(&u8g2, 0, 30, line2);
    u8g2_SendBuffer(&u8g2);
}

static void delay(uint32_t ms) {
    uint32_t start = millis();
    while ((int32_t)(millis() - start - ms) < 0) {}
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

    i2c_init(15); // 400 kHz @ 25 MHz
    while (1) {
        if(!s_oled_ok) s_oled_ok = i2c_probe(0x3C); // Try i2c address 0x3C (OLED)
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            if (!s_oled_ok) led1.blink(600u);
            
            if(s_oled_ok && !s_oled_init) {
                // Diagnostic blinks: 1=before Setup, 2=before InitDisplay, 3=before PowerSave, 4=before Font → done
                
                for(int i=0;i<8;++i){ led0.blink(150u); delay(250u); }  // 8 blink → entering setup
                u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
                u8g2_SetI2CAddress(&u8g2, 0x3C << 1);

                for(int i=0;i<7;++i){ led0.blink(150u); delay(250u); }  // 7 blinks → before InitDisplay (most likely crash point)

                // Direct i2c_write test: SSD1306 "display off" command before letting u8g2 do it
                // If this blinks FAST → i2c_write works fine at this point
                // If no blink → i2c_write hangs here too
                {
                    static const uint8_t cmd[] = {0x00u, 0xAEu}; // control + display-off
                    bool ok = (i2c_write(0x3C, cmd, 2) == 0);
                    if(ok) { for(int i=0;i<3;++i){ led0.blink(50u); delay(100u); } } // 3 fast = OK
                    else   { for(int i=0;i<3;++i){ led1.blink(500u); delay(600u); } } // 3 slow on led1 = FAIL
                }

                u8g2_InitDisplay(&u8g2);

                for(int i=0;i<6;++i){ led0.blink(150u); delay(250u); }  // 6 blinks → before PowerSave
                u8g2_SetPowerSave(&u8g2, 0);

                for(int i=0;i<5;++i){ led0.blink(150u); delay(250u); }  // 5 blinks → before Font
                u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);

                s_oled_init = true;
            }
            if(s_oled_ok && s_oled_init) {
                oled_show("I2C OK", "OLED found at 0x3C");
            }
        }
    }
}