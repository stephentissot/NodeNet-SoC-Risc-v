#include <cstdint>
#include "bigsister.h"
#include "led.h"
#include "i2c.h"
// #include "u8g2.h"
// #include "u8g2_hal.h"

// Hardware — compile-time constants so GCC emits direct MMIO addresses
static volatile uint32_t* const LED_D2 = reinterpret_cast<volatile uint32_t*>(0x10000000UL);
#define LED0_BASE  0x10000004UL
#define LED1_BASE  0x10000008UL
#define I2C0_BASE  0x10005000UL

static void led_d2_blink()
{
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    *LED_D2 = 1u;delay(200u);*LED_D2 = 0u;delay(200u); // led_d2 on/off
    delay(500u);
}

static void blink_bit_pattern(WbLed& green, WbLed& yellow, uint32_t value, uint32_t bits_to_show)
{
    for (uint32_t bit = 0; bit < bits_to_show; ++bit) {
        if ((value & (1u << bit)) != 0u) {
            green.blink(80u);
        } else {
            yellow.blink(40u);
        }
        delay(320u);
    }
    delay(500u);
}

static void run_i2c_diag(WbLed& green, WbLed& yellow)
{
    volatile uint32_t* const status_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x00UL);
    volatile uint32_t* const fifo_status_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x04UL);
    volatile uint32_t* const addr_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x08UL);
    volatile uint32_t* const cmd_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x0CUL);
    volatile uint32_t* const data_reg = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x10UL);

    const uint32_t initial_status = *status_reg;
    const uint32_t initial_fifo = *fifo_status_reg;
    blink_bit_pattern(green, yellow, initial_status, 4u);
    blink_bit_pattern(yellow, green, initial_fifo, 8u);

    *addr_reg = 0x3Cu;
    for(int i = 0; i< 100; ++i) { asm volatile("nop" ::: "memory"); } // small delay
    delay(1u);
    const uint32_t after_addr_status = *status_reg;
    const uint32_t after_addr_fifo = *fifo_status_reg;
    
    blink_bit_pattern(green, yellow, after_addr_status, 4u);
    blink_bit_pattern(yellow, green, after_addr_fifo, 8u);

    *data_reg = 0x00u;
    for(int i = 0; i< 100; ++i) { asm volatile("nop" ::: "memory"); } // small delay
    *cmd_reg = 0x05u; // START | WRITE
    for(int i = 0; i< 100; ++i) { asm volatile("nop" ::: "memory"); } // small delay
    delay(1u);
    const uint32_t after_cmd_status = *status_reg;
    const uint32_t after_cmd_fifo = *fifo_status_reg;
    blink_bit_pattern(green, yellow, after_cmd_status, 4u);
    blink_bit_pattern(yellow, green, after_cmd_fifo, 8u);

    *cmd_reg = 0x10u; // STOP
    for(int i = 0; i< 100; ++i) { asm volatile("nop" ::: "memory"); } // small delay
    delay(1u);
    const uint32_t after_stop_status = *status_reg;
    const uint32_t after_stop_fifo = *fifo_status_reg;
    blink_bit_pattern(green, yellow, after_stop_status, 4u);
    blink_bit_pattern(yellow, green, after_stop_fifo, 8u);
}

int main(void)
{
    led_d2_blink();
    WbLed  ledGreen(LED0_BASE);
    WbLed  ledYellow(LED1_BASE);
    I2c i2c0(I2C0_BASE);
    bool s_oled_ok = false;
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;

    // Initialize I2C0 with a prescale value for 100 kHz operation at 25 MHz clock
    i2c0.begin(15); // 100 kHz @ 25 MHz

    // Mirror test: read back the prescale registers written by begin(62)
    // volatile uint32_t* const prescale_lo =
    //     reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x18UL);
    // volatile uint32_t* const prescale_hi =
    //     reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x1CUL);

    // const uint8_t lo = static_cast<uint8_t>(*prescale_lo & 0xFFu);
    // const uint8_t hi = static_cast<uint8_t>(*prescale_hi & 0xFFu);
    // const uint16_t mirror_prescale = (static_cast<uint16_t>(hi) << 8) | lo;
    // if (mirror_prescale == 62u) {
    //     for (int i = 0; i < 4; ++i) { ledGreen.blink(100u); delay(400u); }
    // } else {
    //     for (int i = 0; i < 4; ++i) { ledYellow.blink(100u); delay(400u); }
    // }
    // End mirror test
    //run_i2c_diag(ledGreen, ledYellow);
    
    while (1) {
        s_oled_ok = i2c0.probe(0x3C); // Try i2c address 0x3C (OLED)
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            if (!s_oled_ok) ledYellow.blink(600u);
            else ledGreen.blink(600u);
            // if(s_oled_ok && !s_oled_init) {
            //     // blink ledGreen to indicate test number
            //     for(int i=0;i<test;++i){ ledGreen.blink(150u); delay(150u); }
            //     switch (test) {
            //         case 0u:    // Test u2g2 init address : test only u8g2_access() and u8g2_SetI2CAddress()
            //         {
            //             //u8g2_SetI2CAddress(&u8g2, 0x3C << 1);
            //             //for (int i = 0; i < 10; ++i) { ledYellow.blink(50u); delay(10u); }
            //             break;
            //         }
            //         case 1u:
            //         {
            //             // Test i2c0 write with len == 1
            //             const uint8_t len2_test_bytes[2] = {0x00u, 0x00u};
            //             int res = i2c0_write(0x3C, len2_test_bytes, sizeof(len2_test_bytes));
            //             if (res == I2C0_OK) {
            //                 for (int i = 0; i < 10; ++i) { ledYellow.blink(100u); delay(400u); }
            //             }
            //             else if(res == I2C0_NACK) {
            //                 for (int i = 0; i < 2; ++i) { ledYellow.blink(100u); delay(400u); }
            //             }
            //             else if(res == I2C0_TIMEOUT) {
            //                 for (int i = 0; i < 4; ++i) { ledYellow.blink(100u); delay(400u); }
            //             }
            //             else if(res == I2C0_FIFO_ERROR) {
            //                 for (int i = 0; i < 6; ++i) { ledYellow.blink(100u); delay(400u); }
            //             }
            //             break;
            //         }
            //         // case 2u:
            //         // {
            //         //     //Test i2c0 write with len == 3
            //         //     const uint8_t len3_test_bytes[3] = {0x00u, 0x00u, 0x00u};
            //         //     if (i2c0_write(0x3C, len3_test_bytes, sizeof(len3_test_bytes)) == I2C0_OK) {
            //         //         for (int i = 0; i < 10; ++i) { ledYellow.blink(50u); delay(10u); }
            //         //     }
            //         //     break;
            //         // }
            //         case 3u:
            //         {
            //             test = 0u; // Restarting at first test
            //             for(int i=0;i<10;++i){ ledGreen.blink(50u); delay(150u); }
            //             break;
            //         }
            //     }
            //     test++;
            //     //Second test
            //     // const uint8_t len3_test_bytes[3] = {0x00u, 0x00u, 0x00u};
            //     // const int len3_result = i2c0_write(0x3C, len3_test_bytes, sizeof(len3_test_bytes));
            //     // if (len3_result != I2C0_OK) {
            //     //     for (int i = 0; i < 2; ++i) { ledYellow.blink(150u); delay(450u); }
            //     // } else {
            //     //     for (int i = 0; i < 1; ++i) { ledGreen.blink(150u); delay(450u); }
            //     // }
                
            //     // bool oledTest = oled_status_read_test(0x3C);
            //     // if(!oledTest) {
            //     //     for(int i=0;i<3;++i){ ledYellow.blink(150u); delay(450u); }
            //     // } else {
            //     //     for(int i=0;i<2;++i){ ledGreen.blink(150u); delay(450u); }
            //     // }

            //     // for(int i=0;i<4;++i){ ledGreen.blink(150u); delay(450u); }
            //     // u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_access(), U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
            //     // for(int i=0;i<3;++i){ ledGreen.blink(150u); delay(450u); }
            //     // u8g2_InitDisplay(u8g2_access());
            //     // for(int i=0;i<2;++i){ ledGreen.blink(150u); delay(450u); }
            //     // u8g2_SetPowerSave(u8g2_access(), 0);

            //     // for(int i=0;i<1;++i){ ledGreen.blink(150u); delay(450u); }
            //     // u8g2_SetFont(u8g2_access(), u8g2_font_5x7_tf);
            //     // s_oled_init = true;
            // }
            // if(s_oled_ok && s_oled_init) {
            //     oled_show("I2C OK", "OLED found at 0x3C");
            // }
        }
    }
}