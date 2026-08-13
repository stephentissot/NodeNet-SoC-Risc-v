# Stage0 Flash Boot Format

This document defines the stage0 image format used by `boot_stage0.cpp`.

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

`firmware-image` runs `tools/pack_firmware.py` and generates `build/nodenet_riscv.img`.
`firmware-image` runs `tools/pack_firmware.py` and generates `build/nodenet_riscv_app.img`.

## Notes

- Stage0 validates: magic, version, header size, bounds in SDRAM range, header CRC, payload CRC.
- On validation failure, stage0 enters a LED blink fault loop.
- Running firmware from SDRAM requires the application to be linked for SDRAM addresses.
