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

int main(void)
{
    led_d2_blink();
    WbLed  ledGreen(LED0_BASE);
    WbLed  ledYellow(LED1_BASE);
    I2c Wire;
    uint8_t s_oled_ok = I2c::I2C_NACK;
    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;

    // Initialize I2C0 with a prescale value for 100 kHz operation at 25 MHz clock
    Wire.begin(62); // 100 kHz @ 25 MHz

    // Mirror test: read back the prescale registers written by begin(62)
    // volatile uint32_t* const i2c_prescaler  = reinterpret_cast<volatile uint32_t*>(I2C0_BASE + 0x0Cu);
    // if (*i2c_prescaler == 15u) {
    //     for (int i = 0; i < 4; ++i) { ledGreen.blink(100u); delay(400u); }
    // } else {
    //     for (int i = 0; i < 4; ++i) { ledYellow.blink(100u); delay(400u); }
    // }
    // End mirror test    

    // Init screen
    // s_oled_ok = Wire.write(0x3C, {
    //     0x00,       // Control byte: following bytes are commands

    //     0xAE,       // Display OFF

    //     0xD5, 0x80, // Set display clock divide ratio / oscillator frequency
    //                 // 0x80 = default recommended setting

    //     0xA8, 0x3F, // Set multiplex ratio
    //                 // 0x3F = 63 -> 64 MUX for a 128x64 panel

    //     0xD3, 0x00, // Set display offset
    //                 // 0x00 = no vertical offset

    //     0x40,       // Set display start line to 0

    //     0x8D, 0x14, // Enable charge pump
    //                 // 0x14 = internal charge pump ON

    //     0xA1,       // Segment remap
    //                 // Column address 127 is mapped to SEG0
    //                 // Common on many OLED modules depending on orientation

    //     0xC8,       // COM output scan direction remapped
    //                 // Flips vertical scan direction

    //     0xDA, 0x12, // Set COM pins hardware configuration
    //                 // 0x12 is the usual setting for 128x64 SSD1306

    //     0x81, 0xCF, // Set contrast control
    //                 // 0xCF = common default contrast value

    //     0xD9, 0xF1, // Set pre-charge period
    //                 // 0xF1 is typical when using internal charge pump

    //     0xDB, 0x40, // Set VCOMH deselect level
    //                 // 0x40 is a common default

    //     0xA4,       // Resume display from RAM content
    //                 // Opposite of "entire display ON" (0xA5)

    //     0xA6,       // Normal display mode
    //                 // Opposite of inverse display (0xA7)

    //     0xAF        // Display ON
    // });
    
    
    bool test_all_pixels_on = false;
    while (1) {
        s_oled_ok = Wire.write(0x3C, {0x00, 0xAE, 0xD5, 0x80});
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {

            if(s_oled_ok != I2c::I2C_OK) {
                ledYellow.blink(100u);
            } else {
                ledGreen.blink(100u);
            }
            // Toggle screen pixels on/off to test I2C write and screen response
            // const uint8_t cmd = test_all_pixels_on ? 0xA4 : 0xA5;
            // const uint8_t rc = Wire.write(0x3C, {0x00, cmd});
            // test_all_pixels_on = !test_all_pixels_on;

            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
            if (s_oled_ok != I2c::I2C_OK) ledYellow.blink(100u);
            else ledGreen.blink(100u);
            // else {                
                
            //     uint8_t test = Wire.write2(0x3C, 0x00, 0xAF); // Screen ON command
            //     bool isBusy = Wire.isBusy();
            //     bool isBusActive = Wire.isBusActive();
            //     bool isBusControlled = Wire.isBusControlled();
            //     if (test != I2c::I2C_OK)
            //     {
            //         for (int i = 0; i < test; ++i) { ledGreen.blink(100u); delay(300u); }  // one green blink per I2C error code
            //         // Blink the green led to indicate the I2C error code
            //         for (int i = 0; i < 1; ++i) { ledYellow.blink(100u); delay(300u); }  // Test #1 one yellow
            //         for (int i = 0; i < test; ++i) { ledGreen.blink(100u); delay(300u); }  // one green blink per I2C error code
            //         delay(500u);
            //         for (int i = 0; i < 2; ++i) { ledYellow.blink(100u); delay(300u); }  // Test #2 two yellow
            //         if(isBusy) {
            //             for (int i = 0; i < 2; ++i) { ledGreen.blink(100u); delay(300u); } // two green blinks if busy
            //         }
            //         delay(500u);
            //         for (int i = 0; i < 3; ++i) { ledYellow.blink(100u); delay(300u); }  // Test #3 three yellow
            //         if(isBusActive) {
            //             for (int i = 0; i < 2; ++i) { ledGreen.blink(100u); delay(300u); } // two green blinks if bus active
            //         }
            //         delay(500u);
            //         for (int i = 0; i < 4; ++i) { ledYellow.blink(100u); delay(300u); } // Test #4 four yellow
            //         if(isBusControlled) {
            //             for (int i = 0; i < 2; ++i) { ledGreen.blink(100u); delay(300u); } // two green blinks if bus controlled
            //         }
            //         delay(1000u);
            //     }         
            //     (void)Wire.write2(0x3C, 0x00, 0xA5); // Screen ON command
            // }
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