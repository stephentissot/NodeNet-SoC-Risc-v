# NodeNet SoC Risc-v — Colorlight i9 Multi-Node Embedded System

A complete RISC-V System-on-Chip (SoC) design for the Colorlight i9 FPGA board, featuring a PicoRV32 processor, Wishbone bus interconnect, persistent SPI flash storage, I2C peripherals, and distributed RS485 multi-node communication via the NodeNet485 protocol.

## Overview

This project demonstrates a scalable embedded systems design on a cost-effective FPGA:
- **Processor**: PicoRV32 (32-bit RISC-V, bare-metal)
- **Clock**: 25 MHz
- **Memory**: 64 KiB ROM (boot code) + 64 KiB RAM (stack/variables) + 8 MB SDRAM (application/external data)
- **Peripherals**: D2 LED GPIO, RJ45 LED pulse controllers (`wb_led`), UART1 (`wb_uart`), RS485 NodeNet485, I2C master, SDRAM controller
- **Communication**: NodeNet485 @ 1 Mb/s over RS-485 (multi-node capable)
- **Firmware**: C++17, bare-metal, newlib-nano
- **Validated SDRAM path**: boot-time init, 32-bit accesses, partial Wishbone writes, and linker-placed `SDRAM_DATA` variables

## Features

### NodeNet Validation Snapshot (2026-08-11)
- Automatic heartbeat in HDL validated on hardware at ~10 s period.
- RX to local address validated (message accepted and decoded).
- TX path validated end-to-end.
- Runtime baud operation validated at both 115200 and 1 Mb/s.
- Remaining checks planned:
  - Broadcast RX acceptance path.
  - Non-matching destination address ignore path.

### Hardware
- **Wishbone B.4 Bus** interconnect with 32-bit data, 32-bit address
- **PicoRV32 Core**: Open-source RISC-V ISA, ~6K LUT footprint
- **Memory Map**:
  - `0x00000000–0x0000FFFF`: 64 KiB boot ROM
  - `0x00010000–0x0001FFFF`: 64 KiB RAM (stack, BSS, heap)
  - `0x10000000`: D2 LED GPIO (1-bit output)
  - `0x10000004`: RJ45 LED0 (`wb_led` one-shot pulse)
  - `0x10000008`: RJ45 LED1 (`wb_led` one-shot pulse)
  - `0x10004000`: UART1 (`wb_uart`, RX/TX FIFO MMIO)
  - `0x10005000`: I2C0 master (8 registers @ 4-byte stride)
  - `0x10006000`: NodeNet485 RS-485 mailbox (8 registers, 1 Mb/s)
  - `0x20000000–0x207FFFFF`: 8 MB SDRAM (application / framebuffer / logs)

### Peripherals
- **NodeNet485 Module** (`wb_nodenet.sv`):
  - Multi-node RS-485 communication protocol
  - Baud rate: runtime divisor (validated at 115200 and 1 Mb/s)
  - HDLC-style framing with parity bits and CRC
  - Mailbox-based Wishbone interface for TX/RX messages
  - Internal message buffering (no external SDRAM FIFO usage)
  - Anti-collision backoff (address-based delay)
  - Periodic heartbeat for node discovery
  - Priority-based transmission (LOW/NORMAL/HIGH)
  - Full C++ firmware API in `include/nodenet.h`
  - Supports unicast, broadcast, and heartbeat messages
  - RX decode error reporting and TX scheduling status
  - Wishbone register interface (0x10006000)

- **I2C Module** (`wb_i2c.sv`):
  - Wraps Alex Forencich's `i2c_master_wbs_16` core
  - Hardware command + write + read FIFOs (32 entries each)
  - Configurable speed (default 100 kHz, up to 400 kHz @ 25 MHz)
  - Drives SCL/SDA open-drain (external 4.7 kΩ pullup required)
  - Wishbone address: 0x10005000

- **UART1 Module** (`wb_uart.sv`):
  - Wishbone UART wrapper with RX/TX FIFOs (`DATA`, `STATUS`, `BAUD`)
  - Uses shared `uart_simple.sv` UART core
  - Intended for firmware serial console / general UART text I/O
  - Wishbone address: 0x10004000

- **SPI Flash Module** (`wb_flash.sv`, `spi_master.sv`):
  - Wishbone interface to on-board SPI flash (W25Q64, 8 MB)
  - Memory layout: 2 MB boot config (protected) + 16 KB parameters + 5.75 MB app
  - C++ firmware API for parameter storage (like Arduino preferences)
  - Boot region protection: firmware rejects writes/erases to 0x000000–0x1FFFFF
  - Parameter storage at 0x200000–0x203FFF (16 KB, 4 sectors)
  - Wishbone address: 0x10007000
  - See [src/wbDevices/README_FLASH.md](src/wbDevices/README_FLASH.md) for details
### Firmware
- **C++17 bare-metal** (`firmware/main.cpp`):
  - Compiled with `riscv-none-elf-g++` — no Arduino, no OS
  - `newlib-nano` provides `memcpy`/`strlen`/etc.
  - Dead code elimination (`-ffunction-sections -Wl,--gc-sections`)
  - Global C++ constructors called in `start.S` via `.init_array`
  - **NodeNet485 echo loop**: Listens for messages, echoes responses
  - D2 activity heartbeat: non-blocking software toggle every 500 ms

- See [src/firmware/README.md](src/firmware/README.md) for full peripheral usage guide.
- See [src/wbDevices/README_NODENET.md](src/wbDevices/README_NODENET.md) for protocol details.

## Building

### Prerequisites
- Yosys (synthesis)
- NextPNR (place & route)
- ecppack (bitstream generation)
- RISC-V GCC toolchain (`riscv-none-elf-gcc` ≥ 12, with C++ support)
- Make

> **After cloning**, fetch the u8g2 submodule:
> ```bash
> git submodule update --init
> ```

### Build Steps

```bash
# Full build (firmware + FPGA)
make all

# Firmware only
make firmware-build

# Firmware test build (uses src/firmware/test_main.cpp)
make firmware-test

# Full bring-up build (test firmware + FPGA bitstream)
make bringup

# FPGA synthesis only (requires pre-built firmware)
make

# Clean all artifacts
make clean

# Program FPGA SRAM (volatile)
make ram

# Program SPI flash (persistent, includes unlock during write)
make flash

# Manual flash protection control
make unlock-flash
make lock-flash
```

CPU profile note:
- Default build is configured in fast mode for PicoRV32 in [src/top.sv](src/top.sv) with:
  - `.BARREL_SHIFTER(1)`
  - `.ENABLE_FAST_MUL(1)`
  - `.ENABLE_DIV(1)`
- To switch back to the normal/smaller CPU profile, set those three parameters to `0` in [src/top.sv](src/top.sv) and rebuild (`make clean; make`).

If OpenOCD flash programming fails on your setup, you can switch the flash
targets in `Makefile` to ecpdap. Reference commands:

```make
# ecpdap fallback examples (Colorlight i5/i9 style headers)
# flash:
# 	ecpdap flash unprotect
# 	ecpdap flash write build/top.config
#
# unlock-flash:
# 	ecpdap flash unprotect
#
# lock-flash:
# 	ecpdap flash protect
```

**Output**:
- `src/firmware/build/nodenet_riscv.hex` – Firmware binary (loaded into ROM)
- `build/top.bit` – FPGA bitstream (~300 KB)
- `build/top.json` – Netlist (debug/inspection)

## Memory Layout

```
Address Range               Size      Purpose
────────────────────────────────────────────────────────────
0x00000000–0x0000FFFF     64 KiB   Boot ROM (firmware binary)
0x00010000–0x0001FFFF     64 KiB   RAM (stack, BSS, heap)
────────────────────────────────────────────────────────────
0x10000000                4 B      D2 LED GPIO (bit [0] = LED output)
0x10000004                4 B      RJ45 LED0 (`wb_led` control/status)
0x10000008                4 B      RJ45 LED1 (`wb_led` control/status)
0x10004000                32 B     UART1 (`wb_uart` serial MMIO)
0x10005000                32 B     I2C0 master (8 regs @ 4-byte stride)
0x10006000–0x1000601F     32 B     NodeNet485 mailbox + LED config (RS485, 1 Mb/s)
0x10007000                32 B     SPI Flash controller (W25Q64)
────────────────────────────────────────────────────────────
0x20000000–0x207FFFFF     8 MB     SDRAM — Fully available to firmware (app/buffers/logs)
────────────────────────────────────────────────────────────
0x00000000–0x1FFFFF       2 MB     SPI Flash — FPGA boot config (PROTECTED)
0x200000–0x203FFF         16 KB    SPI Flash — Parameter storage
0x204000–0x7FFFFF         5.75 MB  SPI Flash — Application data
```

## Device Pinout

### RS485 (NodeNet485 Protocol)
- **Pins**: G5 (RX), D16 (TX)
- **Baud Rate**: 1 Mb/s (fixed, configurable via UART parameter)
- **Protocol**: Multi-node RS-485 with HDLC framing, anti-collision, heartbeat
- **Transceiver**: Any RS-485 module with auto-switching (MAX485, SN65HVD11, etc.)
- **Multi-Node**: Up to 20 nodes on shared bus with address-based scheduling
- **Features**: Parity-encoded payload (2x expansion), XOR CRC, priority levels (LOW/NORMAL/HIGH), heartbeat
- **Connector topology**: 2x RJ45 in daisy-chain (IN/OUT), wired pin-to-pin between connectors

NodeNet RJ45 pinout (both connectors):

| RJ45 Pin | Signal |
|---|---|
| 1 | +12V |
| 2 | +12V |
| 3 | RS485 A |
| 4 | GND |
| 5 | GND |
| 6 | RS485 B |
| 7 | GND |
| 8 | GND |

### I2C (D18/D17 pins)
- **SCL**: D18
- **SDA**: D17
- Requires external 4.7 kΩ pullup resistors to 3.3 V on both lines
- Compatible with any I2C device: OLED, sensors, ADC, GPIO expanders…

### GPIO
- **D2 LED**: GPIO output at 0x10000000 (bit [0] = LED state)

### RJ45 LEDs (`wb_led`)
- **LED0 (F5)**: `0x10000004`
- **LED1 (E6)**: `0x10000008`
- Active-low wiring policy: GPIO high = LED OFF, GPIO low = LED ON.
- LPF enables pull-up on these pins for deterministic OFF level during startup.
- Write command bit0 triggers a non-blocking pulse (duration can be overridden by firmware).
- Main firmware currently keeps RJ45 LEDs for dedicated test firmware (`test_main.cpp`).

### NodeNet Activity LEDs (100% hardware)
- **RX activity LED (E5, green)**: default ON, pulses OFF when a valid frame is received.
- **TX activity LED (F4, orange)**: pulses ON when a transmission is queued.
- Active-low with pull-up: idle/high keeps LED OFF unless logic drives low.
- Blink duration is configured by firmware through NodeNet register `0x1000601C` (milliseconds).

### SPI Flash (W25Q64)
- **Pins**: R2 (CS), W2 (MOSI), V2 (MISO)
- **Clock**: Generated internally (10 MHz typical, no GPIO pin)
- **Capacity**: 8 MB (2 MB boot + 16 KB parameters + 5.75 MB app)
- **Boot Protection**: Firmware API rejects writes to 0x000000–0x1FFFFF
- **Parameter Region**: 0x200000–0x203FFF (key-value store, Arduino preferences-like)

## Firmware Examples

See **[src/firmware/README.md](src/firmware/README.md)** for a full guide with code examples for every peripheral.

### Quick-start: UART1 with Serial
```cpp
#include "lib/serial/Serial.h"

constexpr uint32_t UART1_BASE = 0x10004000u;
Serial Serial1(UART1_BASE);

int main() {
  Serial1.begin(115200);
  Serial1.println("UART1 ready");

  for (;;) {
    if (Serial1.available() > 0) {
      int c = Serial1.read();
      if (c >= 0) {
        Serial1.write(static_cast<uint8_t>(c));
      }
    }
  }
}
```

### Quick-start: NodeNet echo
```cpp
#include <stdint.h>
#include "nodenet.h"

#define LED          (*(volatile uint32_t*)0x10000000)
constexpr uint32_t NODENET0_BASE = 0x10006000u;

int main() {
  NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
  for (;;) {
    if (myNodeNet.HasMessage()) {
      NodeNetMessage msg = myNodeNet.ReadMessage();
      if (msg.src_addr != 0) {
        myNodeNet.Send(msg.src_addr, msg.data, msg.len);
      }
      NodeNet::FreeMessage(msg);
    }
    LED ^= 1;
  }
}
```

### Quick-start: OLED via u8g2
```cpp
#include "u8g2_hal.h"

u8g2_t display;

int main() {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &display, U8G2_R0,
        u8x8_byte_i2c_hw,
        u8x8_gpio_delay_hw);
    u8g2_InitDisplay(&display);
    u8g2_SetPowerSave(&display, 0);
    u8g2_ClearBuffer(&display);
    u8g2_SetFont(&display, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&display, 0, 12, "RISC-V SoC");
    u8g2_SendBuffer(&display);
    for (;;);
}
```

## Wishbone Bus Modules

See [src/wbDevices/README.md](src/wbDevices/README.md) for detailed documentation of each Wishbone peripheral module.

## Resource Usage

**Lattice LFE5U-45F-6BG381C** (Colorlight i9):
- Total LUTs: 45K
- Block RAM (DP16KD): 108 (each 16K bits)
- Current design:
  - PicoRV32: ~6K LUT
  - Interconnect: ~1K LUT
  - ROM (64 KiB): 32 blocks
  - RAM (64 KiB): 32 blocks
  - NodeNet485 transport + UART: ~300 LUT
  - SDRAM controller: ~400 LUT
  - GPIO: ~50 LUT
  - **Total: ~35% of available resources**

## Architecture

```
                   +----------------+
                   |   PicoRV32     |
                   +-------+--------+
                           |
                      Wishbone B4
                           |
      +--------+-----------+---------+--------+----------+-------+
      |        |           |         |        |          |       |
      wb_rom  wb_ram  wb_uart  wb_nodenet wb_gpio wb_led wb_sdram wb_i2c
            |         |
            uart_simple  uart_simple
                 |
               NodeNet framing
        LED D2   RJ45 LEDs   M12L64322A  SSD1306
              |                    (8MB SDRAM) OLED
            RS485 Transceiver
                           |
                 Direct RS485 A/B/GND
                           |
                   Field Connectors
```

**Data Flow**:
1. **CPU** (PicoRV32): 32-bit RISC-V, 25 MHz clock
2. **Wishbone B.4 Bus**: 32-bit address, 32-bit data, address decoder for peripherals
3. **Memory & I/O**:
   - `wb_rom`: 64 KiB boot ROM (0x00000000)
   - `wb_ram`: 64 KiB RAM (0x00010000)
   - `wb_sdram`: 8 MB SDRAM (0x20000000)
  - `wb_nodenet`: NodeNet485 mailbox transport (0x10006000)
  - `wb_gpio`: D2 GPIO output (0x10000000)
  - `wb_led`: RJ45 LED pulse controllers (0x10000004 / 0x10000008)
   - `wb_i2c`: I2C master with FIFOs (0x10005000)
4. **NodeNet Transport**: Mailbox-driven TX/RX framing, decode, and heartbeat scheduling
5. **I2C Core**: Wraps Alex Forencich's `i2c_master_wbs_16` with Wishbone interface
6. **RS485 Physical**: Transceiver converts CMOS ↔ RS485 differential signaling
7. **Field Wiring**: Each RS485 channel is exposed as direct A/B/GND connections

## Planned Enhancements

- [x] 8 MB SDRAM controller (`wb_sdram.sv`) with auto-refresh and auto-precharge
- [x] I2C master (`wb_i2c.sv`) — Wishbone wrapper around verilog-i2c
- [x] u8g2 OLED display support (SSD1306 128×64 over I2C)
- [x] C++17 firmware with newlib-nano and dead-code elimination
- [x] SDRAM section in linker script (`sdram.h`, `SDRAM_DATA` macro)
- [ ] NodeNet485 mailbox transport with TX/RX framing and heartbeat (validation pending)
- [ ] UART1–4 implementation and testing
- [ ] RS485 direct connector mapping and labeling verification (A/B/GND)
- [ ] `wb_gpio` control bit for RS485 120R termination switch (74LVC1G66GW,125)
- [ ] Modbus V1 front-end: 4x RS485 modules with direct A/B/GND connectors per channel
- [ ] `wb_gpiopwm` peripheral + firmware API (PKLCS1212E4001-R1 buzzer)
- [ ] TMP117 board temperature sensor + firmware I2C driver/API
- [ ] Display validation: OLED over I2C and LCD over SPI (select final path after tests)
- [ ] Evaluate and select more robust 1 Mb/s transceivers for Modbus and NodeNet
- [ ] Dual Ethernet breakout using i9 onboard Broadcom PHYs (2x RJ45 with magnetics)
- [x] RJ45 LEDs integrated through `wb_led` at dedicated addresses
- [ ] Final RJ45 firmware policy (link/activity/status) for production runtime
- [ ] ESP32 module SPI coprocessor for web API and MQTT bridge
- [ ] Power option study: use ESP32 module 3.3V rail vs dedicated 12V->3.3V buck
- [ ] Modbus RTU library (master/slave modes)
- [ ] NodeNet JSON protocol wrapper (UART4)
- [ ] SPI slave interface for ESP32 co-processor
- [ ] SPI color LCD support (ST7789, 76x284) on Colorlight i9
- [ ] KNX integration with NCN5120 transceiver module
- [ ] Hardware-based CRC accelerator
- [ ] Real-time interrupt controller (RISC-V PLIC)

## Hardware Requirements

- **Colorlight i9 Board** with Lattice LFE5U-45F FPGA
- **USB-C Programmer** (e.g., openFPGALoader, oss-cad-suite)
- **RS485 Transceiver** modules (SN65HVD230, MAX483, etc.)
- **RS485 Field Connectors** (A/B/GND per channel)
- **SSD1306 OLED Module** (I2C, 128×32 or 128×64) + 4.7 kΩ pullup resistors

## Testing

```bash
# Program RAM (requires openFPGALoader)
make ram

# Program Flash (permanent)
make flash

# Explicit flash protection control (optional)
make unlock-flash
make lock-flash

# UART loopback test
# - Send "Hello" via USB-to-UART adapter
# - LED should blink
# - Characters echoed back
```

`make flash` now performs an explicit unlock step in the OpenOCD write command (`flash write_image erase unlock ...`) before erase/program.
`make lock-flash`/`make unlock-flash` call `flash protect ...` through OpenOCD; effectiveness/persistence depends on driver support for the target flash path.

## References

- [PicoRV32 GitHub](https://github.com/YosysHQ/picorv32)
- [Verilog UART IP (Alex Forencich)](https://github.com/alexforencich/verilog-uart) – Core UART module used in wb_uart.sv
- [Verilog I2C IP (Alex Forencich)](https://github.com/alexforencich/verilog-i2c) – Core I2C module used in wb_i2c.sv
- [u8g2 Graphics Library](https://github.com/olikraus/u8g2) – OLED display driver (git submodule)
- [Wishbone B.4 Specification](https://cdn.opencores.org/downloads/wbspec_b4.pdf)
- [LFE5U FPGA Datasheet](https://www.latticesemi.com/en/Products/FPGAs/ECP5)
- [RISC-V Unprivileged ISA Manual](https://riscv.org/specifications/)

## License

See [LICENSE](LICENSE) file.

## Author

**Stephen Tissot**  
Industrial IoT / FPGA Engineering  
https://github.com/stephentissot

---

**Status**: Prototype / Active Development  
**Last Updated**: 2026-07-28
