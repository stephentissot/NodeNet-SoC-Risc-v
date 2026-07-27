# NodeNet SoC Risc-v — Colorlight i9 Multi-Node Embedded System

A complete RISC-V System-on-Chip (SoC) design for the Colorlight i9 FPGA board, featuring a PicoRV32 processor, Wishbone bus interconnect, persistent SPI flash storage, I2C peripherals, and distributed RS485 multi-node communication via the NodeNet485 protocol.

## Overview

This project demonstrates a scalable embedded systems design on a cost-effective FPGA:
- **Processor**: PicoRV32 (32-bit RISC-V, bare-metal)
- **Clock**: 25 MHz
- **Memory**: 64 KiB ROM (boot code) + 64 KiB RAM (stack/variables) + 8 MB SDRAM (application/external data)
- **Peripherals**: LED GPIO, RS485 NodeNet485, I2C master, SDRAM controller
- **Communication**: NodeNet485 @ 1 Mb/s over RS-485 (multi-node capable)
- **Firmware**: C++17, bare-metal, newlib-nano

## Features

### Hardware
- **Wishbone B.4 Bus** interconnect with 32-bit data, 32-bit address
- **PicoRV32 Core**: Open-source RISC-V ISA, ~6K LUT footprint
- **Memory Map**:
  - `0x00000000–0x0000FFFF`: 64 KiB boot ROM
  - `0x00010000–0x0001FFFF`: 64 KiB RAM (stack, BSS, heap)
  - `0x10000000`: LED GPIO (1-bit output)
  - `0x10005000`: I2C0 master (8 registers @ 4-byte stride)
  - `0x10006000`: NodeNet485 RS-485 mailbox (7 registers, 1 Mb/s)
  - `0x20000000–0x207FFFFF`: 8 MB SDRAM (application / framebuffer / logs)

### Peripherals
- **NodeNet485 Module** (`wb_nodenet.sv`):
  - Multi-node RS-485 communication protocol
  - Baud rate: 1 Mb/s
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
  - Wraps Alex Forencich's `i2c_master_wbs_8` core
  - Hardware command + write + read FIFOs (32 entries each)
  - Configurable speed (default 100 kHz, up to 400 kHz @ 25 MHz)
  - Drives SCL/SDA open-drain (external 4.7 kΩ pullup required)  - Wishbone address: 0x10005000

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
  - LED blink pattern for boot indication

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

# FPGA synthesis only (requires pre-built firmware)
make

# Clean all artifacts
make clean
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
0x10000000                4 B      LED GPIO (bit [0] = LED output)
0x10005000                32 B     I2C0 master (8 regs @ 4-byte stride)
0x10006000–0x1000601B     28 B     NodeNet485 mailbox (RS485, 1 Mb/s)
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
- **Pins**: H16 (RX), H17 (TX)
- **Baud Rate**: 1 Mb/s (fixed, configurable via UART parameter)
- **Protocol**: Multi-node RS-485 with HDLC framing, anti-collision, heartbeat
- **Transceiver**: Any RS-485 module with auto-switching (MAX485, SN65HVD11, etc.)
- **Multi-Node**: Up to 20 nodes on shared bus with address-based scheduling
- **Features**: Parity-encoded payload (2x expansion), XOR CRC, priority levels (LOW/NORMAL/HIGH), heartbeat

### I2C (pmodg connector)
- **SCL**: H4 (pmodg[0])
- **SDA**: G3 (pmodg[1])
- Requires external 4.7 kΩ pullup resistors to 3.3 V on both lines
- Compatible with any I2C device: OLED, sensors, ADC, GPIO expanders…

### GPIO
- **D2 LED**: GPIO output at 0x10000000 (bit [0] = LED state)

### SPI Flash (W25Q64)
- **Pins**: R2 (CS), W2 (MOSI), V2 (MISO)
- **Clock**: Generated internally (10 MHz typical, no GPIO pin)
- **Capacity**: 8 MB (2 MB boot + 16 KB parameters + 5.75 MB app)
- **Boot Protection**: Firmware API rejects writes to 0x000000–0x1FFFFF
- **Parameter Region**: 0x200000–0x203FFF (key-value store, Arduino preferences-like)

## Firmware Examples

See **[src/firmware/README.md](src/firmware/README.md)** for a full guide with code examples for every peripheral.

### Quick-start: NodeNet echo
```cpp
#include <stdint.h>
#include "nodenet.h"

#define LED          (*(volatile uint32_t*)0x10000000)

int main() {
  nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
  for (;;) {
    if (nodenet0_has_message()) {
      NodeNetMessage msg = nodenet0_read();
      if (msg.src_addr != 0) {
        nodenet0_send(msg.src_addr, msg.data, msg.len);
      }
      nodenet0_free_message(msg);
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
      +--------+-----------+--------+----------+-------+
      |        |           |        |          |       |
         wb_rom   wb_ram   wb_nodenet wb_gpio   wb_sdram wb_i2c
              |            |          |        |
               uart_simple   LED D2  M12L64322A  SSD1306
              |                    (8MB SDRAM) OLED
            RS485 Transceiver
                           |
                    TMUX4051 Mux Array
                           |
                    RJ45 Connectors
```

**Data Flow**:
1. **CPU** (PicoRV32): 32-bit RISC-V, 25 MHz clock
2. **Wishbone B.4 Bus**: 32-bit address, 32-bit data, address decoder for peripherals
3. **Memory & I/O**:
   - `wb_rom`: 64 KiB boot ROM (0x00000000)
   - `wb_ram`: 64 KiB RAM (0x00010000)
   - `wb_sdram`: 8 MB SDRAM (0x20000000)
  - `wb_nodenet`: NodeNet485 mailbox transport (0x10006000)
   - `wb_gpio`: GPIO output (0x10000000)
   - `wb_i2c`: I2C master with FIFOs (0x10005000)
4. **NodeNet Transport**: Mailbox-driven TX/RX framing, decode, and heartbeat scheduling
5. **I2C Core**: Wraps Alex Forencich's `i2c_master_wbs_8` with Wishbone interface
6. **RS485 Physical**: Transceiver converts CMOS ↔ RS485 differential signaling
7. **Multiplexing**: TMUX4051 arrays route A/B pairs to correct RJ45 connectors

## Planned Enhancements

- [x] 8 MB SDRAM controller (`wb_sdram.sv`) with auto-refresh and auto-precharge
- [x] I2C master (`wb_i2c.sv`) — Wishbone wrapper around verilog-i2c
- [x] u8g2 OLED display support (SSD1306 128×64 over I2C)
- [x] C++17 firmware with newlib-nano and dead-code elimination
- [x] SDRAM section in linker script (`sdram.h`, `SDRAM_DATA` macro)
- [x] NodeNet485 mailbox transport with TX/RX framing and heartbeat
- [ ] UART1–4 implementation and testing
- [ ] RS485 multiplexer firmware control
- [ ] Modbus RTU library (master/slave modes)
- [ ] NodeNet JSON protocol wrapper (UART4)
- [ ] SPI slave interface for ESP32 co-processor
- [ ] Hardware-based CRC accelerator
- [ ] Real-time interrupt controller (RISC-V PLIC)

## Hardware Requirements

- **Colorlight i9 Board** with Lattice LFE5U-45F FPGA
- **USB-C Programmer** (e.g., openFPGALoader, oss-cad-suite)
- **RS485 Transceiver** modules (SN65HVD230, MAX483, etc.)
- **TMUX4051 Multiplexers** (8 units, 2 per RJ45)
- **74HC138 Decoder** (1 unit, for mux selection)
- **RJ45 Connectors** (4 units)
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
**Last Updated**: 2026-07-25
