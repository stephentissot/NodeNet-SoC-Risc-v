# Hardware Test Guide

## Scope

Current boot architecture:

1. Stage0 in ROM BRAM (`boot_stage0.hex`)
2. App image in SPI flash (`nodenet_riscv_app.img`), copied to SDRAM and executed

This guide focuses on:

1. Build/program commands by change type
2. Boot robustness tests (missing image, bad CRC, bad size)
3. Ultra-short bench checklist

## Build and Program Matrix

1. HDL changed, quick validation in FPGA RAM:
   - `make all`
   - `make ram`

2. HDL changed, persistent cold-boot in FPGA flash:
   - `make all`
   - `make flash`
   - `make flash-fw` (reprogram app image partition after flash erase)

3. Firmware app only changed:
   - `make flash-fw-run`

4. Stage0 changed:
   - `make all`
   - `make ram`

5. Build artifacts only:
   - Stage0 ROM image: `make firmware-build`
   - App image: `make firmware-image`

## Boot Robustness Tests

Prepare fault images:

```bash
make firmware-image-tests
```

Run scenarios:

1. Missing image
   - `make flash-fw-test-missing`
   - `make ram-fast`
   - expected blink code: 2 pulses

2. Invalid payload CRC
   - `make flash-fw-test-crc`
   - `make ram-fast`
   - expected blink code: 10 pulses

3. Invalid image size/range
   - `make flash-fw-test-size`
   - `make ram-fast`
   - expected blink code: 7 pulses

Recovery to normal app:

```bash
make flash-fw-run
```

## Bench Sheet (Ultra Short)

Board / setup:

- Date:
- Board ID:
- Branch/commit:
- Operator:

Checklist:

| Test | Command(s) | Expected | PASS/FAIL | Observations |
|---|---|---|---|---|
| Baseline valid app boot | `make flash-fw-run` | App starts normally |  |  |
| Missing image fault | `make flash-fw-test-missing` + `make ram-fast` | D2 = 2 pulses loop |  |  |
| Bad CRC fault | `make flash-fw-test-crc` + `make ram-fast` | D2 = 10 pulses loop |  |  |
| Bad size fault | `make flash-fw-test-size` + `make ram-fast` | D2 = 7 pulses loop |  |  |
| Recovery after faults | `make flash-fw-run` | App starts normally again |  |  |

## Notes

1. `make firmware-test` is intentionally disabled (legacy flow removed).
2. If `make flash` is run, re-run `make flash-fw` to restore firmware app partition.
