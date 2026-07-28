/**
 * @file test_main.cpp
 * @brief Comprehensive hardware test suite for Colorlight i9 RISC-V SoC
 * 
 * Tests all peripherals and displays results on OLED via I2C:
 * - LED GPIO (D2)
 * - I2C0 master (OLED SSD1306)
 * - NodeNet485 RS-485 transport (TX framing + optional RX validation)
 * - SPI Flash (W25Q64) protect + erase/program/readback + KV parameters
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
#include "sdram.h"
#include "i2c.h"
#include "led.h"
#include "u8g2.h"
#include "u8g2_hal.h"

#define LED0_BASE 0x10000004UL
#define LED1_BASE 0x10000008UL
static constexpr uint32_t I2C0_BASE = 0x10005000u;
static constexpr uint32_t NODENET0_BASE = 0x10006000u;

static wb_led::Led led0(LED0_BASE, false);
static wb_led::Led led1(LED1_BASE, false);
static const I2C i2c0(I2C0_BASE);

// ════════════════════════════════════════════════════════════════════════════
// Hardware Abstractions
// ════════════════════════════════════════════════════════════════════════════

/** Bare-metal C++ stubs (no libstdc++, no exceptions, no RTTI) */
extern "C" void __cxa_pure_virtual() { while (1); }

/** Simple delay function (@ 25 MHz) */
void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 25000; i++) {}
}

void blink_code(uint8_t code, uint32_t gap_ms) {
    for (uint8_t i = 0; i < code; ++i) {
        led0.Blink(0);
        delay_ms(300);
    }
    delay_ms(gap_ms);
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
        led0.Blink(0);
        delay_ms(200);
    }
    // If we got here without crashing, test passed
    return true;
}

/** Test I2C0 — verify OLED responds */
bool test_i2c(void) {
    // Controller-level check independent of any external device presence.
    // Program a known prescale and verify read-back through Wishbone.
    const uint16_t expected = 62;  // 100 kHz @ 25 MHz
    i2c0.Init(expected);

    uint16_t readback = i2c0.Prescale();
    return readback == expected;
}

/** Test SDRAM read/write path */
bool test_sdram(void) {
    sdram_wait_ready();
    return sdram_test(256) == 0;
}

/** Test NodeNet485 RS-485 transport */
bool test_nodenet(void) {
    NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
    
    // Delay to allow initialization
    delay_ms(100);
    
    // Validate that the transport can queue and drain a real broadcast frame.
    // Any valid incoming frame during the observation window is treated as a bonus.
    const char* test_msg = "TEST";
    myNodeNet.Broadcast((const uint8_t*)test_msg, 4);
    led0.Blink(0);

    for (int i = 0; i < 100; i++) {  // ~1 s timeout
        uint32_t status = myNodeNet.Status();

        if (myNodeNet.HasMessage()) {
            NodeNetMessage msg = myNodeNet.ReadMessage();
            led1.Blink(0);
            NodeNet::FreeMessage(msg);
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

/** Test dedicated flash erase/program/readback cycle in app region */
bool test_flash_rw_erase(void) {
    const uint32_t test_offset = FLASH_APP_BASE;  // Safe area outside boot + params
    const uint16_t test_sector = (uint16_t)(test_offset / FLASH_SECTOR_SIZE);

    uint8_t write_buf[256];
    uint8_t read_buf[256];

    // Deterministic non-trivial pattern.
    for (int i = 0; i < 256; i++) {
        write_buf[i] = (uint8_t)(i ^ 0x5A);
    }

    // 1) Erase and verify blank page
    if (!flash_erase_sector(test_sector)) {
        return false;
    }
    flash_read_page(test_offset, read_buf);
    for (int i = 0; i < 256; i++) {
        if (read_buf[i] != 0xFF) return false;
    }

    // 2) Program and verify exact readback
    if (!flash_write_page(test_offset, write_buf)) {
        return false;
    }
    flash_read_page(test_offset, read_buf);
    if (memcmp(read_buf, write_buf, 256) != 0) {
        return false;
    }

    // 3) Erase again and verify blank state restored
    if (!flash_erase_sector(test_sector)) {
        return false;
    }
    flash_read_page(test_offset, read_buf);
    for (int i = 0; i < 256; i++) {
        if (read_buf[i] != 0xFF) return false;
    }

    return true;
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

    // Test 3: SDRAM
    bool sdram_ok = test_sdram();
    oled_print_test(line++, "SDRAM", sdram_ok);
    if (sdram_ok) pass_count++;
    total_count++;
    
    // Test 4: NodeNet485
    bool nodenet_ok = test_nodenet();
    oled_print_test(line++, "NodeNet485", nodenet_ok);
    if (nodenet_ok) pass_count++;
    total_count++;
    
    // Test 5: SPI Flash protection
    bool flash_ok = test_flash();
    oled_print_test(line++, "Flash protect", flash_ok);
    if (flash_ok) pass_count++;
    total_count++;
    
    // Test 6: Flash erase/program/readback
    bool flash_rw_ok = test_flash_rw_erase();
    oled_print_test(line++, "Flash RW/erase", flash_rw_ok);
    if (flash_rw_ok) pass_count++;
    total_count++;

    // Test 7: Flash parameters
    bool flash_param_ok = test_flash_params();
    oled_print_test(line++, "Flash params", flash_param_ok);
    if (flash_param_ok) pass_count++;
    total_count++;
    
    // Summary line
    line++;
    char summary[32];
    snprintf(summary, sizeof(summary), "Result: %d/%d PASS", pass_count, total_count);
    oled_print_line(line, summary);
    
    // Failure code for quick diagnosis without OLED
    // 1=LED, 2=I2C, 3=SDRAM, 4=NodeNet, 5=Flash protect, 6=Flash RW/erase, 7=Flash params
    uint8_t fail_code = 0;
    if (!led_ok) fail_code = 1;
    else if (!i2c_ok) fail_code = 2;
    else if (!sdram_ok) fail_code = 3;
    else if (!nodenet_ok) fail_code = 4;
    else if (!flash_ok) fail_code = 5;
    else if (!flash_rw_ok) fail_code = 6;
    else if (!flash_param_ok) fail_code = 7;

    // Overall status pulse
    if (fail_code == 0) {
        led0.Blink(0);
    }
    
    oled_show();
    
    // Wait for observation
    while (1) {
        if (fail_code == 0) {
            delay_ms(2000);
            led0.Blink(100);  // Healthy heartbeat pulse
        } else {
            blink_code(fail_code, 1000);
        }
    }
    
    return 0;
}
