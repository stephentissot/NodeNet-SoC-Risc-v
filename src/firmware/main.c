#include <stdbool.h>
#include <stdint.h>
#include "bigsister.h"
#include "sdram.h"
#include "led.h"
#include "i2c.h"
#include "i2c_regs.h"

#define K_BLINK_PERIOD_MS 2000u

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

    bool s_oled_ok = false;
    bool s_oled_init = false;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + K_BLINK_PERIOD_MS;
    *LED_D2 = 1u;

    i2c_begin();
    

    // // 1. Écriture manuelle de la valeur 42 dans le registre de vitesse (Offset d'octet 4)
    // // On passe bien l'adresse de base ET l'offset    
    // i2c_master_reg_wr(I2C0_BASE, I2C_MASTER_SPD, 42u);
    
    // // Barrière mémoire indispensable pour forcer l'exécution de l'écriture Wishbone
    // __asm__ volatile("" ::: "memory");

    // // 2. Relecture immédiate avec les deux paramètres requis
    // uint32_t read_back = i2c_master_reg_rd(I2C0_BASE, I2C_MASTER_SPD); 

    // // 3. Remise en place de la configuration nominale pour la suite du programme
    // i2c_setClock(100000u);

    // // 4. Code de diagnostic par LED basé sur la valeur miroir
    // if (read_back == 42u) {
    //     // LE BUS ET LES TAILLES DE REGISTRES SONT ENFIN ALIGNÉS !
    //     for(int i=0; i<3; ++i) { wbLedBlink(LED0_BASE, 150u); delay(450u); } // 3 LEDs vertes
    // } else {
    //     for(int i=0; i<3; ++i) { wbLedBlink(LED1_BASE, 150u); delay(450u); } // 3 LEDs jaunes
    // }

    uint8_t test = 0u;
    while (1) {          
        // Test in the loop to see sda scl trace on the logic analyzer
        i2c_beginTransmission(0x3C);
        i2c_write(0x00u);        
        int tx_status = 1;
        tx_status = i2c_endTransmission();
        s_oled_ok = (tx_status == 0u);
        
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += K_BLINK_PERIOD_MS;



    
            if (!s_oled_ok){
                for(int i=0;i<tx_status;++i){ wbLedBlink(LED0_BASE, 150u); delay(150u); }
                wbLedBlink(LED1_BASE, 600u);
            }
            else {
                wbLedBlink(LED0_BASE, 60u);
            }
            // if(s_oled_ok){
            //     oled.test();
            // }            
            // if(s_oled_ok && !s_oled_init) {
            //     // blink led0 to indicate test number
            //     for(int i=0;i<test;++i){ led0.blink(150u); delay(150u); }
            //     switch (test) {
            //         case 0u:    // Test u2g2 init address : test only u8g2_access() and u8g2_SetI2CAddress()
            //         {
            //             u8g2_SetI2CAddress(u8g2_access(), 0x3C << 1);
            //             //for (int i = 0; i < 10; ++i) { led1.blink(50u); delay(10u); }
            //             break;
            //         }
            //         case 1u:
            //         {
            //             // Test i2c0 write with len == 1
            //             // const uint8_t len2_test_bytes[2] = {0x00u, 0x00u};
            //             // int res = i2c0.i2c_write(0x3C, len2_test_bytes, sizeof(len2_test_bytes));
            //             // if (res == I2C_OK) {
            //             //     for (int i = 0; i < 10; ++i) { led1.blink(100u); delay(400u); }
            //             // }
            //             // else if(res == I2C_NACK) {
            //             //     for (int i = 0; i < 2; ++i) { led1.blink(100u); delay(400u); }
            //             // }
            //             // else if(res == I2C_TIMEOUT) {
            //             //     for (int i = 0; i < 4; ++i) { led1.blink(100u); delay(400u); }
            //             // }
            //             // else if(res == I2C_FIFO_ERROR) {
            //             //     for (int i = 0; i < 6; ++i) { led1.blink(100u); delay(400u); }
            //             // }
            //             // break;
            //         }
            //         case 2u:
            //         {
            //             //Test i2c0 write with len == 3
            //             // const uint8_t len3_test_bytes[3] = {0x00u, 0x00u, 0x00u};
            //             // if (i2c0.i2c_write(0x3C, len3_test_bytes, sizeof(len3_test_bytes)) == I2C_OK) {
            //             //     for (int i = 0; i < 10; ++i) { led1.blink(50u); delay(10u); }
            //             // }
            //             // break;
            //         }
            //         case 3u:
            //         {
            //             test = 0u; // Restarting at first test
            //             for(int i=0;i<10;++i){ led0.blink(50u); delay(150u); }
            //             break;
            //         }
            //     }
            //     test++;
            //     //Second test
            //     // const uint8_t len3_test_bytes[3] = {0x00u, 0x00u, 0x00u};
            //     // const int len3_result = i2c0_write(0x3C, len3_test_bytes, sizeof(len3_test_bytes));
            //     // if (len3_result != I2C0_OK) {
            //     //     for (int i = 0; i < 2; ++i) { led1.blink(150u); delay(450u); }
            //     // } else {
            //     //     for (int i = 0; i < 1; ++i) { led0.blink(150u); delay(450u); }
            //     // }
                
            //     // bool oledTest = oled_status_read_test(0x3C);
            //     // if(!oledTest) {
            //     //     for(int i=0;i<3;++i){ led1.blink(150u); delay(450u); }
            //     // } else {
            //     //     for(int i=0;i<2;++i){ led0.blink(150u); delay(450u); }
            //     // }

            //     // for(int i=0;i<4;++i){ led0.blink(150u); delay(450u); }
            //     // u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_access(), U8G2_R0, u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
            //     // for(int i=0;i<3;++i){ led0.blink(150u); delay(450u); }
            //     // u8g2_InitDisplay(u8g2_access());
            //     // for(int i=0;i<2;++i){ led0.blink(150u); delay(450u); }
            //     // u8g2_SetPowerSave(u8g2_access(), 0);

            //     // for(int i=0;i<1;++i){ led0.blink(150u); delay(450u); }
            //     // u8g2_SetFont(u8g2_access(), u8g2_font_5x7_tf);
            //     // s_oled_init = true;
            // }
            // if(s_oled_ok && s_oled_init) {
            //     oled_show("I2C OK", "OLED found at 0x3C");
            // }
        }
    }
}