#include <cstdint>
#include "bigsister.h"
#include "sdram.h"
#include "led.h"
#include "lib/i2c/i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL

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
    led_d2_blink();
    //sdram_wait_ready();
    WbLed  led0(LED0_BASE);
    WbLed  led1(LED1_BASE);
    WbI2c  i2c0(I2C0_BASE);
    bool s_oled_ok = false;
    bool s_oled_init = false;
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;
    i2c0.begin();
    i2c0.setClock(100000u); // 100 kHz @ 25 MHz
    uint8_t test = 0u;
    while (1) {
        if(!s_oled_ok){
            i2c0.beginTransmission(0x3C); // probe for OLED at 0x3C
            i2c0.write(0x3C); // probe for OLED at 0x3C
            int tx_status = i2c0.endTransmission();
            if(tx_status == 0) {
                s_oled_ok = true;
            }
        }
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            if (!s_oled_ok) led1.blink(600u);
            else led0.blink(60u);            
        }
    }
}