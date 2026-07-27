# Hardware Test Suite

## Overview

The Colorlight i9 SoC includes a comprehensive **test firmware** (`test_main.cpp`) that exercises all peripherals and displays results on an OLED display via I2C.

Test coverage:
- ✓ LED GPIO (D2 blink)
- ✓ I2C0 master (SSD1306 OLED communication)
- ✓ NodeNet485 RS-485 communication
- ✓ SPI Flash read/write/protection

## Building the Test Firmware

By default, the project builds with `main.cpp` (NodeNet485 echo loop). To build the test suite:

```bash
# Using bash (recommended for Unix-like environment):
cd d:\FPGA\projects\colorlight_blink
bash -c "make clean && make -C src/firmware MAIN_SRC=test_main.cpp && make all"

# Or using PowerShell on Windows (with subshell):
cd d:\FPGA\projects\colorlight_blink
powershell -Command {& make clean; make -C src/firmware -e MAIN_SRC=test_main.cpp; make all}

# Or modify src/firmware/Makefile to use test_main.cpp by default (not recommended for CI)
sed -i 's/^MAIN_SRC \?= main.cpp/MAIN_SRC ?= test_main.cpp/' src/firmware/Makefile
make clean && make all
```

**Output**:
- `src/firmware/build/nodenet_riscv.hex` – Test firmware (42 KB)
- `build/top.bit` – FPGA bitstream (~391 KB)

## Programming the FPGA

Once bitstream is built:

```bash
# Program to RAM (volatile, lost on power cycle):
make ram

# Program to Flash (persistent):
make flash
```

## Using the Test Suite

Once the Colorlight i9 is programmed with the test bitstream:

1. **Power on the board** — SystemClock starts, PicoRV32 boots
2. **OLED display shows test results** in real-time:
   ```
   Colorlight i9 Test Suite
   LED: OK
   I2C0 (OLED): OK
   NodeNet485: OK/FAIL
   Flash protect: OK
   Flash params: OK
   Result: 5/5 PASS
   ```
3. **LED behavior**:
   - If all tests pass: LED stays ON
   - If any test fails: LED stays OFF
   - After results displayed: LED blinks every 2 seconds for observation

## Test Descriptions

### LED (D2)
- **Purpose**: Verify GPIO output functionality
- **Action**: Blinks LED 3 times (100 ms on, 100 ms off)
- **Pass Criteria**: Completes without crash

### I2C0 (OLED)
- **Purpose**: Verify I2C master communication with SSD1306
- **Action**: Initializes OLED display and writes test results
- **Pass Criteria**: OLED displays text without errors

### NodeNet485
- **Purpose**: Verify RS-485 communication protocol
- **Action**: Initializes node at address 0x01, sends self-test message
- **Pass Criteria**: Receives echo back (or gracefully times out without error)
- **Note**: May fail if no other nodes on RS-485 bus (expected behavior)

### Flash Protection
- **Purpose**: Verify boot region is protected from accidental overwrites
- **Action**: 
  1. Attempts write to protected region (0x100) — should be rejected
  2. Attempts write to safe region (0x200000) — should succeed
  3. Reads back and verifies data integrity
- **Pass Criteria**: All three steps succeed

### Flash Parameters
- **Purpose**: Verify key-value parameter storage works
- **Action**:
  1. Writes key "test_key" with value "hello" to flash
  2. Reads it back and compares
- **Pass Criteria**: Values match exactly

## Troubleshooting

### "I2C0 (OLED): FAIL"
- **Cause**: OLED not responding on I2C
- **Check**:
  - SDA/SCL pins connected to pmodg[1] and pmodg[0]
  - 4.7 kΩ pullup resistors installed on both lines
  - SSD1306 I2C address is 0x3C (default for 128×64)
  - OLED powered (3.3 V)

### "NodeNet485: FAIL" (expected in single-node setup)
- **Cause**: No other nodes on RS-485 bus to echo message
- **Expected**: Test may timeout gracefully without error
- **Fix**: Add another Colorlight i9 or RS-485 device to bus

### "Flash protect: FAIL"
- **Cause**: Boot region protection check failed
- **Check**:
  - SPI flash is properly connected (CS on R2, MOSI on W2, MISO on V2)
  - flash.h has correct #define for FLASH_BOOT_SIZE = 2 MB
  - No corrupted flash (try full reset/reprogram)

### "Flash params: FAIL"
- **Cause**: Parameter write/read failed
- **Check**:
  - SPI flash responds to reads (would also fail "Flash protect")
  - Parameter region not corrupted (manual inspection via SPI)
  - 16 KB parameter region at 0x200000–0x203FFF not full

## Switching Back to Normal Firmware

To restore the NodeNet485 echo loop:

```bash
make clean && make all
# This builds with main.cpp (default)
```

## Extending the Test Suite

To add new tests to `test_main.cpp`:

1. **Create a test function**:
   ```cpp
   bool test_my_feature(void) {
       // Test implementation
       // Return true if pass, false if fail
       return true;
   }
   ```

2. **Add to main() test loop**:
   ```cpp
   bool my_ok = test_my_feature();
   oled_print_test(line++, "My Feature", my_ok);
   if (my_ok) pass_count++;
   total_count++;
   ```

3. **Rebuild**:
   ```bash
   bash -c "make clean && make -C src/firmware MAIN_SRC=test_main.cpp && make all"
   ```

## Performance Notes

- **OLED rendering**: ~200 ms (I2C @ 100 kHz, 128×64 full framebuffer)
- **LED blink test**: ~600 ms
- **NodeNet485 test**: ~500 ms timeout
- **Flash test**: ~10 ms (read/write/erase in safe region)
- **Total runtime**: ~1.5 seconds from boot to final test display

## See Also

- [README.md](../README.md) – Main project overview
- [src/firmware/README.md](../../firmware/README.md) – Firmware peripheral guide
- [src/wbDevices/README_FLASH.md](../../wbDevices/README_FLASH.md) – Flash module details
- [src/wbDevices/README_NODENET.md](../../wbDevices/README_NODENET.md) – NodeNet485 protocol
