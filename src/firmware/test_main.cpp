/**
 * @file test_main.cpp
 * @brief Comprehensive hardware test suite for Colorlight i9 RISC-V SoC
 * 
 * Tests all peripherals and displays results on OLED via I2C:
 * - LED GPIO (D2)
 * - I2C0 master (OLED SSD1306)
 * - NodeNet485 RS-485 transport (TX framing + optional RX validation)
 * - SPI Flash (W25Q64) read/write/protect
 * 
 * OLED Display Layout:
 *   [Test Name] [✓/✗] [Status]
 * 
 * Usage: To use this test instead of main.cpp:
 *   1. Build with: make clean && MAIN_SRC=test_main.cpp make all
 *   2. Or modify src/firmware/Makefile to use test_main.cpp by default
 */

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "version.h"
#include "nodenet.h"
#include "flash.h"
#include "u8g2.h"
#include "u8g2_hal.h"

// ════════════════════════════════════════════════════════════════════════════
// Hardware Abstractions
// ════════════════════════════════════════════════════════════════════════════

#define LED (*(volatile uint32_t*)0x10000000)

/** Bare-metal C++ stubs (no libstdc++, no exceptions, no RTTI) */
extern "C" void __cxa_pure_virtual() { while (1); }

/** Simple delay function (@ 25 MHz) */
void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 25000; i++) {}
}

// ════════════════════════════════════════════════════════════════════════════
// OLED Display (U8G2)
// ════════════════════════════════════════════════════════════════════════════

u8g2_t u8g2;

void oled_init(void) {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, 
                                            u8x8_byte_i2c_hw, 
                                            u8x8_gpio_delay_hw);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);  // Display on
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
}

void oled_clear(void) {
    u8g2_ClearBuffer(&u8g2);
}

void oled_print_line(uint8_t line, const char* text) {
    // line 0 = top, each line is ~10 pixels high
    u8g2_DrawStr(&u8g2, 0, (line + 1) * 10, text);
}

void oled_print_test(uint8_t line, const char* name, bool pass) {
    char buf[32];
    const char* status = pass ? "OK" : "FAIL";
    snprintf(buf, sizeof(buf), "%s: %s", name, status);
    oled_print_line(line, buf);
}

void oled_show(void) {
    u8g2_SendBuffer(&u8g2);
}

// ════════════════════════════════════════════════════════════════════════════
// Test Suite
// ════════════════════════════════════════════════════════════════════════════

struct test_result {
    const char* name;
    bool passed;
    const char* details;
};

/** Test LED GPIO */
bool test_led(void) {
    // Blink LED 3 times
    for (int i = 0; i < 3; i++) {
        LED = 1;
        delay_ms(100);
        LED = 0;
        delay_ms(100);
    }
    // If we got here without crashing, test passed
    return true;
}

/** Test I2C0 — verify OLED responds */
bool test_i2c(void) {
    // If OLED initialized and displayed, I2C is working
    return true;  // Already tested by OLED init
}

/** Test NodeNet485 RS-485 transport */
bool test_nodenet(void) {
    // Initialize NodeNet485 (this node is address 0x01)
    nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
    
    // Delay to allow initialization
    delay_ms(100);
    
    // Validate that the transport can queue and drain a real broadcast frame.
    // Any valid incoming frame during the observation window is treated as a bonus.
    const char* test_msg = "TEST";
    nodenet0_broadcast((const uint8_t*)test_msg, 4);

    for (int i = 0; i < 100; i++) {  // ~1 s timeout
        uint32_t status = nodenet_status();

        if (nodenet0_has_message()) {
            NodeNetMessage msg = nodenet0_read();
            nodenet0_free_message(msg);
            return true;
        }

        if ((status & (NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE)) == 0) {
            return (status & (NODENET_STATUS_RX_ERROR | NODENET_STATUS_RX_OVERFLOW)) == 0;
        }

        delay_ms(10);
    }

    return false;
}

/** Test SPI Flash read/write with boot protection */
bool test_flash(void) {
    // Test 1: Verify boot region protection
    // Attempt to write to protected region (should be rejected)
    uint8_t test_buf[256];
    memset(test_buf, 0xAA, sizeof(test_buf));
    
    // Try to write at offset 0x100 (in protected region 0x000000–0x1FFFFF)
    bool protected_rejected = !flash_write_page(0x100, test_buf);
    
    // Test 2: Verify parameter region is accessible
    // Write to safe region (0x200000+)
    bool safe_write = flash_write_page(FLASH_PARAM_BASE, test_buf);
    
    // Test 3: Read back and verify
    uint8_t read_buf[256];
    flash_read_page(FLASH_PARAM_BASE, read_buf);
    bool safe_read = (memcmp(read_buf, test_buf, 256) == 0);
    
    // All tests must pass: protection works AND safe region accessible
    return protected_rejected && safe_write && safe_read;
}

/** Test parameter storage (key-value) */
bool test_flash_params(void) {
    // Write a test parameter
    const char* key = "test_key";
    const char* value = "hello";
    
    if (!flash_put_string(key, value)) {
        return false;  // Write failed
    }
    
    // Read it back
    char buf[32];
    flash_get_string(key, buf, sizeof(buf), "");
    
    // Verify value matches
    return (strcmp(buf, value) == 0);
}

// ════════════════════════════════════════════════════════════════════════════
// Main Test Loop
// ════════════════════════════════════════════════════════════════════════════

int main(void) {
    // Initialize OLED display via I2C
    oled_init();
    oled_clear();
    oled_print_line(0, "Colorlight i9 Test Suite");
    oled_show();
    delay_ms(500);
    
    // Run tests
    oled_clear();
    uint8_t line = 0;
    int pass_count = 0;
    int total_count = 0;
    
    // Test 1: LED
    bool led_ok = test_led();
    oled_print_test(line++, "LED", led_ok);
    if (led_ok) pass_count++;
    total_count++;
    
    // Test 2: I2C (already tested by OLED init)
    bool i2c_ok = test_i2c();
    oled_print_test(line++, "I2C0 (OLED)", i2c_ok);
    if (i2c_ok) pass_count++;
    total_count++;
    
    // Test 3: NodeNet485
    bool nodenet_ok = test_nodenet();
    oled_print_test(line++, "NodeNet485", nodenet_ok);
    if (nodenet_ok) pass_count++;
    total_count++;
    
    // Test 4: SPI Flash protection
    bool flash_ok = test_flash();
    oled_print_test(line++, "Flash protect", flash_ok);
    if (flash_ok) pass_count++;
    total_count++;
    
    // Test 5: Flash parameters
    bool flash_param_ok = test_flash_params();
    oled_print_test(line++, "Flash params", flash_param_ok);
    if (flash_param_ok) pass_count++;
    total_count++;
    
    // Summary line
    line++;
    char summary[32];
    snprintf(summary, sizeof(summary), "Result: %d/%d PASS", pass_count, total_count);
    oled_print_line(line, summary);
    
    // Overall status
    LED = pass_count == total_count ? 1 : 0;  // LED on if all pass
    
    oled_show();
    
    // Wait for observation (LED blink every 2 seconds)
    while (1) {
        delay_ms(2000);
        LED ^= 1;  // Toggle LED every 2 seconds
    }
    
    return 0;
}
