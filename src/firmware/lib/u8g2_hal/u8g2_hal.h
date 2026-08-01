/**
 * u8g2_hal.h — u8g2 HAL for wb_i2c (Wishbone I2C MMIO peripheral)
 *
 * Usage:
 *   #include "u8g2_hal.h"
 *
 *   u8g2_t display;
 *
 *   // 128×64 SSD1306 at address 0x3C, full framebuffer mode
 *   u8g2_Setup_ssd1306_i2c_128x64_noname_f(
 *       &display, U8G2_R0,
 *       u8x8_byte_i2c_hw,        // I2C callback (this file)
 *       u8x8_gpio_delay_hw       // GPIO & delay callback (this file)
 *   );
 *   u8g2_InitDisplay(&display);
 *   u8g2_SetPowerSave(&display, 0);
 *
 *   // Draw
 *   u8g2_ClearBuffer(&display);
 *   u8g2_SetFont(&display, u8g2_font_ncenB08_tr);
 *   u8g2_DrawStr(&display, 0, 10, "Hello RISC-V!");
 *   u8g2_SendBuffer(&display);
 *
 * I2C address note:
 *   u8g2 stores addresses already in 8-bit form (7-bit << 1).
 *   u8x8_GetI2CAddress() returns this value; we shift right by 1 to get 7-bit.
 *   Most SSD1306 modules use 0x3C (128×32) or 0x3D (128×64).
 *
 * Hardware: SCL → D18, SDA → D17 on Colorlight i9
 *           External 4.7 kΩ pullup resistors to 3.3 V required on both lines.
 */

#ifndef U8G2_HAL_H
#define U8G2_HAL_H

// i2c.h contains a C++ class — must be included OUTSIDE extern "C"
#include "i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <u8g2.h>

/**
 * u8x8_byte_i2c_hw — Hardware I2C byte-level callback for u8g2.
 *
 * Handles: BYTE_INIT, BYTE_START_TRANSFER, BYTE_SEND, BYTE_END_TRANSFER.
 * Uses the wb_i2c MMIO peripheral (i2c_master_wbs_8 core).
 * Supports arbitrarily large transfers by stalling on full FIFOs.
 */
uint8_t u8x8_byte_i2c_hw(u8x8_t *u8x8, uint8_t msg,
                          uint8_t arg_int, void *arg_ptr);

/**
 * u8x8_gpio_delay_hw — GPIO and delay callback for u8g2.
 *
 * Handles: GPIO_AND_DELAY_INIT, DELAY_MILLI, DELAY_10MICRO, DELAY_100NANO.
 * No real GPIO control needed for I2C (SCL/SDA handled by hardware).
 * Uses RISC-V rdcycle CSR for accurate timing at 25 MHz.
 */
uint8_t u8x8_gpio_delay_hw(u8x8_t *u8x8, uint8_t msg,
                            uint8_t arg_int, void *arg_ptr);

#ifdef __cplusplus
}

// Must be called once before any u8g2 display setup.
// Shares the same I2C bus instance used by main() — avoids two independent
// states (tx_buffer, tx_length, …) on the same MMIO peripheral.
void u8g2_hal_set_i2c(I2C* bus);
#endif

#endif /* U8G2_HAL_H */
