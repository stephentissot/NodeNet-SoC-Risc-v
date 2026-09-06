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
