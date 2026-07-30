#include <cstdint>
#include <cstdio>
#include "bigsister.h"
#include "led.h"
#include "i2c.h"
#include "u8g2.h"
#include "u8g2_hal.h"
#include "version.h"

// Hardware setup
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE 0x10000004UL
#define LED1_BASE 0x10000008UL


// ─── OLED ────────────────────────────────────────────────────────────────────
static u8g2_t u8g2;
static bool s_oled_ok = false;

// Quick I2C probe: send 1 byte (control byte 0x00) and check for ACK.
static bool i2c_probe(const I2C &bus, uint8_t addr7bit) {
    const uint8_t ctrl = 0x00u;
    return bus.Write(addr7bit, &ctrl, 1) == 0;
}

static void oled_init() {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
                                            u8x8_byte_i2c_hw,
                                            u8x8_gpio_delay_hw);
    u8g2_SetI2CAddress(&u8g2, 0x3C << 1);  // explicit 0x3C (7-bit)
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

int main(void)
{
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;

    // ── Direct MMIO sanity test ───────────────────────────────────────────────
    {
        volatile uint32_t* led0_reg = reinterpret_cast<volatile uint32_t*>(LED0_BASE);
        volatile uint32_t* led1_reg = reinterpret_cast<volatile uint32_t*>(LED1_BASE);

        // Phase 1: SET_DEFAULT (bit1) — already confirmed working
        // 5x ON/OFF blink @ 500ms with D2 in sync
        for (int step = 0; step < 10; step++) {
            bool on = (step % 2 == 0);
            *LED_D2   = on ? 0u : 1u;
            *led0_reg = on ? 0x6u : 0x2u;
            *led1_reg = on ? 0x6u : 0x2u;
            uint32_t t = millis();
            while ((int32_t)(millis() - t - 500) < 0) {}
        }
        // After phase 1: default_state = 0 (OFF)

        // Phase 2: TRIGGER (bit0) — raw write with default RTL blink cycles (100ms)
        // LED0/LED1 should flash ON for 100ms, then OFF. Repeat 5x with 1s gap.
        // D2 blinks FAST (100ms) to show we're in phase 2.
        for (int i = 0; i < 5; i++) {
            *LED_D2   = 0u;   // D2 ON (marker: phase 2 active)
            *led0_reg = 0x1u; // TRIGGER bit0=1, cycles=0 → use RTL default (100ms)
            *led1_reg = 0x1u;
            uint32_t t = millis();
            while ((int32_t)(millis() - t - 200) < 0) {}  // wait 200ms (LED should flash)
            *LED_D2   = 1u;   // D2 OFF
            uint32_t t2 = millis();
            while ((int32_t)(millis() - t2 - 800) < 0) {}  // wait 800ms gap
        }

        // Phase 3: TRIGGER with explicit 1s blink (25M cycles = 1 second ON)
        // Very hard to miss if it works.
        *LED_D2   = 0u;
        // make_blink_cmd(25000000) = (25000000 << 3) | 1 = 0xBEBC201
        *led0_reg = 0xBEBC201u;
        *led1_reg = 0xBEBC201u;
        uint32_t t3 = millis();
        while ((int32_t)(millis() - t3 - 1500) < 0) {}  // wait 1.5s
        *LED_D2 = 1u;

        // Reset to OFF
        *led0_reg = 0x2u;
        *led1_reg = 0x2u;
    }
    // ──────────────────────────────────ake ───────────────────────────────────────

    // Init I2C prescale then probe OLED before full init
    static constexpr uint32_t I2C0_BASE = 0x10005000u;
    static const I2C i2c0(I2C0_BASE);
    i2c0.Init(15); // 400 kHz @ 25 MHz
    s_oled_ok = i2c_probe(i2c0, 0x3C);
    if (s_oled_ok) {
        oled_init();
        oled_show("NodeNet SoC", "i9 v7.2", FIRMWARE_VERSION);
    }
    WbLed led0(LED0_BASE);  // Not static: no guard byte, base_ always set correctly
    WbLed led1(LED1_BASE);
    while (1) {
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;

            if (s_oled_ok) {
                char buf[24];
                snprintf(buf, sizeof(buf), "t=%lums", (unsigned long)now_ms);
                oled_show("NodeNet SoC", buf);
            }

            led0.blink(300u);
            led1.blink(600u);
        }
    }
}