# Stage0 Flash Boot Format

This document defines the stage0 image format used by `src/bootloader/boot_stage0.cpp`.

## Flash Layout (current)

- `0x000000-0x1FFFFF`: FPGA configuration area (reserved)
- `0x200000-0x203FFF`: parameter area
- `0x204000-0x243FFF`: FlashDB KV area
- `0x244000-...`: stage0 firmware payload area

Stage0 currently reads the image header from `0x244000`.

## Header

The header is defined by `include/firmware_image.h` and occupies 64 bytes:

- `magic` (`0x46574E4E`)
- `header_version` (`1`)
- `header_size` (`64`)
- `flags`
- `load_addr`
- `entry_addr`
- `image_size`
- `image_crc32` (CRC32 payload only)
- `header_crc32` (CRC32 over the full 64-byte header with `header_crc32=0`)
- `image_offset` (default `64`)
- `reserved[24]`

All fields are little-endian.

## Build usage

From `src/firmware`:

```bash
make bootloader-build
make firmware-image APP_LOAD_ADDR=0x20000000 APP_ENTRY_ADDR=0x20000000
```

`bootloader-build` now consumes stage0 sources from `src/bootloader/`, while `firmware-image` still packages the SDRAM application from `src/firmware/`.

`firmware-image` runs `tools/pack_firmware.py` and generates `build/nodenet_riscv_app.img`.

From project root, firmware-only flash update flow is:

```bash
make flash-fw-check   # build+package+structural verification
make flash-fw         # program image at 0x244000
make flash-fw-run     # flash-fw + FPGA RAM reload (stage0 restart)
```

## Notes

- Stage0 validates: magic, version, header size, bounds in SDRAM range, header CRC, payload CRC.
- On validation failure, stage0 enters a LED blink fault loop.
- Running firmware from SDRAM requires the application to be linked for SDRAM addresses.

## Stage0 Blink Codes

Stage0 reports boot errors on D2 LED with pulse-count patterns:

- 1 pulse: header read failure from flash
- 2 pulses: image absent (erased header, `0xFFFFFFFF` magic)
- 3 pulses: invalid magic
- 4 pulses: unsupported header version
- 5 pulses: invalid header size
- 6 pulses: header CRC mismatch
- 7 pulses: invalid image size/range
- 8 pulses: invalid entry address range
- 9 pulses: payload read failure during copy
- 10 pulses: payload CRC mismatch
- 11 pulses: unexpected return from application entry

## Hardware Robustness Checklist

Preconditions:

1. Build/program FPGA once with stage0 in ROM (`make all` then `make ram` if needed).
2. Ensure firmware app slot offset remains `0x244000`.

Test assets:

1. Generate test images: `make firmware-image-tests`

Test 1 — Missing image:

1. Program missing image pattern: `make flash-fw-test-missing`
2. Reload FPGA SRAM image: `make ram-fast`
3. Expected: stage0 stays in fault loop, D2 blinks 2 pulses.

Test 2 — Invalid payload CRC:

1. Program CRC-corrupted image: `make flash-fw-test-crc`
2. Reload FPGA SRAM image: `make ram-fast`
3. Expected: stage0 copies then fails CRC, D2 blinks 10 pulses.

Test 3 — Invalid size:

1. Program size-invalid image: `make flash-fw-test-size`
2. Reload FPGA SRAM image: `make ram-fast`
3. Expected: stage0 rejects header/range, D2 blinks 7 pulses.

Recovery:

1. Reprogram valid app image: `make flash-fw-run`
2. Expected: normal boot and jump to application.
