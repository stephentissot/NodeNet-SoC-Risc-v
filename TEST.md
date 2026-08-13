# Hardware Test Suite

## Overview

The Colorlight i9 SoC now boots in 2 stages:

- Stage0 in ROM BRAM (`boot_stage0.hex`)
- Application image in SPI flash (`nodenet_riscv_app.img`) copied to SDRAM at boot

This file covers:

1. Boot robustness campaign (stage0 + flash image faults)
2. Legacy peripheral test firmware notes (`test_main.cppold`)
2. Boot robustness campaign (missing image / invalid CRC / invalid size)

Test coverage:
- ✓ LED GPIO (D2 blink)
- ✓ I2C0 master (SSD1306 OLED communication)
- ✓ SDRAM controller (read/write validation)
- ✓ NodeNet485 RS-485 communication
- ✓ SPI Flash protection
- ✓ SPI Flash erase/program/readback cycle
- ✓ SPI Flash key-value parameter storage

## Legacy Peripheral Test Firmware

The historical peripheral test program currently exists as `src/firmware/test_main.cppold`.
It is not wired into the default stage0 app-image flow and currently requires source refresh
to match the latest firmware APIs.

## Programming the FPGA

Common programming flows:

```bash
# Build stage0 + HDL bitstream from sources
make all

# Program FPGA SRAM (volatile HDL)
make ram

# Program FPGA configuration flash (persistent HDL cold-boot image)
make flash

# Program only firmware app image partition, then restart stage0 quickly
make flash-fw-run
```

## Build/Program Matrix

Use this matrix depending on what changed:

1. HDL changed (`src/top.sv`, `src/wbDevices/*.sv`, etc.) and you want volatile test in FPGA RAM:
  - `make all`
  - `make ram`

2. HDL changed and you want persistent cold-boot from flash:
  - `make all`
  - `make flash`
  - `make flash-fw` (because `make flash` can erase app partition)

3. Firmware app only changed (`main.cpp`, libs, logic) and stage0 unchanged:
  - `make flash-fw-run`

4. Stage0 changed (`boot_stage0.cpp`, boot header logic):
  - `make all`
  - `make ram` for quick validation
  - optional persistent path: `make flash` then `make flash-fw`

5. Build only stage0 ROM artifact (no programming):
  - `make firmware-build`

6. Build only app image artifact (no programming):
  - `make firmware-image`

7. Legacy one-shot test firmware build target:
  - `make firmware-test` (currently disabled with explicit error)

## Boot Robustness Campaign

Generate fault injection images:

```bash
make firmware-image-tests
```

Run each scenario:

1. Missing image
  - `make flash-fw-test-missing`
  - `make ram-fast`
  - Expected D2 code: 2 pulses

2. Invalid payload CRC
  - `make flash-fw-test-crc`
  - `make ram-fast`
  - Expected D2 code: 10 pulses

3. Invalid image size/range
  - `make flash-fw-test-size`
  - `make ram-fast`
  - Expected D2 code: 7 pulses

Recovery to valid firmware:

```bash
make flash-fw-run
```

## Using the Test Suite

Once the Colorlight i9 is programmed with the test bitstream:

1. **Power on the board** — SystemClock starts, PicoRV32 boots
2. **OLED display shows test results** in real-time:
   ```
   Colorlight i9 Test Suite
   LED: OK
   I2C0 (OLED): OK
  SDRAM: OK
   NodeNet485: OK/FAIL
   Flash protect: OK
  Flash RW/erase: OK
   Flash params: OK
  Result: 7/7 PASS
   ```
3. **LED behavior**:
  - If all tests pass: LED heartbeat (toggle every 2 seconds)
  - If a test fails: LED blinks a failure code repeatedly
    - 1 blink: LED test
    - 2 blinks: I2C/OLED test
    - 3 blinks: SDRAM test
    - 4 blinks: NodeNet test
    - 5 blinks: Flash protection test
    - 6 blinks: Flash erase/program/readback test
    - 7 blinks: Flash parameter test

## Test Descriptions

### LED (D2)
- **Purpose**: Verify GPIO output functionality
- **Action**: Blinks LED 3 times (100 ms on, 100 ms off)
- **Pass Criteria**: Completes without crash

### I2C0 (OLED)
- **Purpose**: Verify I2C master communication with SSD1306
- **Action**: Initializes OLED display and writes test results
- **Pass Criteria**: OLED displays text without errors

### SDRAM
- **Purpose**: Verify external SDRAM init + read/write path
- **Action**: Waits for SDRAM controller readiness, then runs a small word-pattern test
- **Pass Criteria**: No mismatch during read-back

### NodeNet485
- **Purpose**: Verify RS-485 communication protocol
- **Action**: Initializes node at address 0x01, sends a broadcast test frame, then watches TX completion and optional received traffic
- **Pass Criteria**: TX mailbox drains without RX decode/overflow error; any valid reply also counts as pass
- **Note**: In a single-node setup, this validates TX framing only. With other nodes or a PC sniffer/injector, it also validates receive decoding.

### Flash Protection
- **Purpose**: Verify boot region is protected from accidental overwrites
- **Action**: 
  1. Attempts write to protected region (0x100) — should be rejected
  2. Attempts write to safe region (0x200000) — should succeed
  3. Reads back and verifies data integrity
- **Pass Criteria**: All three steps succeed

### Flash RW/Erase
- **Purpose**: Verify sector erase and page program behavior in a safe flash region
- **Action**:
  1. Erases one sector at `FLASH_APP_BASE`
  2. Verifies erased page reads as `0xFF`
  3. Programs a 256-byte pattern and verifies exact read-back
  4. Erases again and verifies blank state restored
- **Pass Criteria**: All four steps succeed

### Flash Parameters
- **Purpose**: Verify key-value parameter storage works
- **Action**:
  1. Writes key "test_key" with value "hello" to flash
  2. Reads it back and compares
- **Pass Criteria**: Values match exactly

## Troubleshooting

### Day-1 Bring-up Checklist (No Instruments Needed)

1. Program RAM with test firmware (`MAIN_SRC=test_main.cpp`).
2. Confirm D2 LED changes state within 2-3 seconds after reset.
3. If OLED is connected, check the 7 test lines and final 7/7 summary.
4. If OLED is not connected, use LED code blinks to identify first failing stage.
5. After all pass, rebuild default firmware (`make clean; make all`) and reflash.

### "I2C0 (OLED): FAIL"
- **Cause**: OLED not responding on I2C
- **Check**:
  - SDA/SCL pins connected to pmodg[1] and pmodg[0]
  - 4.7 kΩ pullup resistors installed on both lines
  - SSD1306 I2C address is 0x3C (default for 128×64)
  - OLED powered (3.3 V)

### "NodeNet485: FAIL"
- **Cause**: TX never drained, or the decoder reported timeout / framing / overflow
- **Check**:
  - H16/H17 wiring to RS485 transceiver
  - 1 Mb/s support on the connected nodes/sniffer
  - Shared ground and correct A/B polarity on the bus
  - Another node or PC tool is not flooding malformed frames

### "Flash protect: FAIL"
- **Cause**: Boot region protection check failed
- **Check**:
  - SPI flash is properly connected (CS on R2, MOSI on W2, MISO on V2)
  - flash.h has correct #define for FLASH_BOOT_SIZE = 2 MB
  - No corrupted flash (try full reset/reprogram)

### "Flash RW/erase: FAIL"
- **Cause**: Sector erase or page-program cycle failed
- **Check**:
  - SPI wiring and flash power integrity
  - Flash busy handling in wb_flash (WIP polling)
  - Test area at `FLASH_APP_BASE` is not being used by another flow

### "Flash params: FAIL"
- **Cause**: Parameter write/read failed
- **Check**:
  - SPI flash responds to reads (would also fail "Flash protect"/"Flash RW/erase")
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
- **Flash tests**: ~30-80 ms total (protect + erase/program/readback + params)
- **Total runtime**: ~1.5-2.0 seconds from boot to final test display

## See Also

- [README.md](../README.md) – Main project overview
- [src/firmware/README.md](../../firmware/README.md) – Firmware peripheral guide
- [src/wbDevices/README_FLASH.md](../../wbDevices/README_FLASH.md) – Flash module details
- [src/wbDevices/README_NODENET.md](../../wbDevices/README_NODENET.md) – NodeNet485 protocol
