# ESP32 Firmware

This directory contains the ESP32 sidecar firmware used for:

- web-based configuration
- MQTT publication
- Home Assistant discovery
- SPI transport toward the FPGA mailbox bridge

## Selected Stack

- IDE: VS Code
- build system: PlatformIO
- framework: ESP-IDF
- board id: `wemos_d1_mini32`

## Web Transport Policy

- current target: stay on `HTTP` with the current `wemos_d1_mini32` hardware
- reason: this keeps RAM risk lower while the main runtime, UI, and web application bricks are still being integrated
- planned evolution: if the project later moves to an `ESP32-S3 N16R8`, the intended next step is to keep the setup portal on `HTTP` and move the main web application only to `HTTPS`
- certificate direction for that future `HTTPS` app path: allow uploading a server certificate and private key rather than hard-coding a single certificate into the firmware
- decision gate: continue advancing the application on the current board first, then re-evaluate the `ESP32-S3 N16R8` migration once the main features are in place

## Current Scope

The initial skeleton only brings up:

- boot logging on UART
- ST7789 bring-up through a direct ESP-IDF SPI driver using the validated panel wiring and init sequence
- a one-shot ST7789 full-screen red / green / blue test at boot
- FPGA SPI-link initialization on the same shared SPI bus

## Display Driver

- implementation: local ESP-IDF SPI driver in `src/display_st7789.cpp`
- panel model: `ST7789`
- current bring-up test: one-shot full-screen red / green / blue fills at boot
- note: the init sequence remains aligned with the working `76x284` ST7789 bring-up that was validated during the Arduino reference phase
- bus speed: `10 MHz` for the display device on the shared bus

## UI Library Direction

- recommended library: `LVGL`
- reason: it fits the current pure `ESP-IDF` stack well and expects exactly the kind of flush callback that `display_st7789.cpp` can provide
- keep the current ST7789 driver as the low-level panel backend; do not replace it with a framework-specific display wrapper
- avoid `TFT_eSPI` here: it is more display-driver oriented than UI-oriented and would pull the project back toward the Arduino path we intentionally removed
- next integration step: add a small `lvgl_port` that allocates one or two partial RGB565 draw buffers and forwards `flush_cb` rectangles into `display_st7789::blit_rgb565()`

## Shared SPI Bus

- bus init: shared once through `src/spi_bus_shared.cpp`
- display device: `GPIO5` chip select, SPI mode `3`, `10 MHz`
- FPGA device: `GPIO27` chip select, SPI mode `0`, `1 MHz`
- note: the ESP32 can switch speed per SPI device even when `SCK/MOSI/MISO` are physically shared

## Expected Commands

From this directory:

```powershell
pio run
pio run -t upload
pio device monitor
```

## Wiring Summary

- Shared SPI bus
	- `GPIO18` -> FPGA `J16` and ST7789 `SCL` for `SCK`
	- `GPIO23` -> FPGA `J18` and ST7789 `SDA` for `MOSI`
- FPGA link
	- `GPIO19` <- FPGA `P16` for `MISO`
	- `GPIO27` -> FPGA `N4` for `CS`
	- `GPIO32` <- FPGA `M3` for `IRQ`
- ST7789 control
	- `GPIO5` -> ST7789 `CS`
	- `GPIO16` -> ST7789 `DC`
	- `GPIO17` -> ST7789 `RST`
	- `BL` tied to `GND` and not software-controlled
- Reserved sideband
	- `GPIO33` <-> FPGA `T3` kept reserved for a future FPGA sideband signal

`GPIO33` is a good candidate for a future display `TE` input if you dedicate it to the display.
Do not share that same wire between the FPGA reserved sideband and the display reset or tearing signal.

The current bring-up build is now back on pure ESP-IDF with the FPGA link reattached on the shared SPI bus.
The next step is to validate FPGA transactions on this baseline, then raise the FPGA-side SPI speed if the slave logic and signal integrity allow it.
