# FPGA Hello World - Colorlight i9 RISC-V SoC

A complete RISC-V System-on-Chip (SoC) design for the Colorlight i9 FPGA board, featuring a PicoRV32 processor, Wishbone bus interconnect, UART communication with FIFO buffering, and RS485 multiplexing for Modbus RTU industrial protocols.

## Overview

This project demonstrates a scalable embedded systems design on a cost-effective FPGA:
- **Processor**: PicoRV32 (32-bit RISC-V, bare-metal)
- **Clock**: 25 MHz
- **Memory**: 64 KiB ROM (boot code) + 64 KiB RAM (stack/variables)
- **Peripherals**: LED GPIO, UART with FIFOs, RS485 multiplexing
- **Protocol Support**: Modbus RTU (4 UARTs), NodeNet JSON (planned)

## Features

### Hardware
- **Wishbone B.4 Bus** interconnect with 32-bit data, 32-bit address
- **PicoRV32 Core**: Open-source RISC-V ISA, ~6K LUT footprint
- **Memory Map**:
  - `0x00000000–0x0000FFFF`: 64 KiB boot ROM
  - `0x00010000–0x0002FFFF`: 64 KiB RAM
  - `0x10000000`: LED GPIO (1-bit output)
  - `0x10001000–0x10004000`: 4× UART peripherals (planned)

### Peripherals
- **UART Module** (`wb_uart.sv`): 
  - 16-byte RX and TX circular FIFOs
  - Programmable baud rate (default ~115200 @ 25 MHz)
  - Status register with FIFO flags
  - Ready for RS485 RS485 driver integration

- **RS485 Multiplexing** (planned):
  - 4 TMUX4051 muxes (2 per RJ45 connector, A/B pair routing)
  - 3-bit address bus + 74HC138 decoder = only 4 GPIO pins
  - Dynamic pair selection at runtime or boot

### Firmware
- **C Bootloader** (`firmware/main.c`):
  - UART echo demo (loopback test)
  - LED toggle via GPIO
  - SysTick-based delay functions

## Building

### Prerequisites
- Yosys (synthesis)
- NextPNR (place & route)
- ecppack (bitstream generation)
- RISC-V GCC toolchain (for firmware)
- Make

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
- `src/firmware/build/blink.hex` – Firmware binary (loaded into ROM)
- `build/top.bit` – FPGA bitstream (~290 KB)
- `build/top.json` – Netlist (debug/inspection)

## Memory Layout

```
Address Range       Size    Purpose
─────────────────────────────────────────
0x00000000–0x0000FFFF  64 KiB  Boot ROM (firmware binary)
0x00010000–0x0002FFFF  64 KiB  RAM (stack, BSS, heap)
─────────────────────────────────────────
0x10000000           4 B     LED GPIO (bit [0] = LED output)
0x10001000           12 B    UART0 (DATA @ +0x0, STATUS @ +0x4, BAUD @ +0x8)
0x10002000           12 B    UART1 (planned)
0x10003000           12 B    UART2 (planned)
0x10004000           12 B    UART3 (planned)
```

## Device Pinout

### RJ45 Connectors (Modbus RS485)
- **RJ45_0**: UART0 (pair 0–3 routed via TMUX0)
- **RJ45_1**: UART1 (pair 0–3 routed via TMUX1)
- **RJ45_2**: UART2 (pair 0–3 routed via TMUX2)
- **RJ45_3**: UART3 (pair 0–3 routed via TMUX3)

Each RJ45 exposes 4 twisted pairs; multiplexer allows any UART to route to any pair dynamically.

### GPIO
- **D2 LED**: GPIO output (indicates CPU activity)
- **Multiplexer Control** (4 pins):
  - 3× Address lines (A0–A2) – shared across all 8 muxes
  - 1× Decoder select (8-way demux via 74HC138)

## Firmware Examples

### UART Echo
```c
#define UART0_DATA     0x10001000
#define UART0_STATUS   0x10001004
#define UART_RX_EMPTY  (1u << 0)
#define UART_TX_FULL   (1u << 3)

volatile uint32_t *uart0_data   = (volatile uint32_t *)UART0_DATA;
volatile uint32_t *uart0_status = (volatile uint32_t *)UART0_STATUS;

// Transmit
void uart0_putc(char c) {
    while (*uart0_status & UART_TX_FULL);  // Wait for space
    *uart0_data = c;
}

// Receive
char uart0_getc(void) {
    while (*uart0_status & UART_RX_EMPTY);  // Wait for data
    return (char)(*uart0_data & 0xFF);
}
```

### Configure UART Baud Rate
```c
#define UART0_BAUD 0x10001008

void uart0_init(uint16_t prescale) {
    volatile uint32_t *uart0_baud = (volatile uint32_t *)UART0_BAUD;
    *uart0_baud = prescale;  // Default 27 for ~115200 @ 25 MHz
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
  - UART0 + FIFOs: ~300 LUT
  - GPIO: ~50 LUT
  - **Total: ~33% of available resources**

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      Wishbone Bus (32-bit)              │
├────┬──────────────┬──────┬──────────┬──────────────────┤
│    │              │      │          │                  │
│PicoRV32         ROM    RAM       LED GPIO            UART0–3
│    │              │      │          │                  │
│    └──────────────┴──────┴──────────┴──────────────────┘
│                         │
│                    Address Decoder
│
└─ 25 MHz Clock, 64K ROM, 64K RAM, Modbus RS485 routing
```

## Planned Enhancements

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

## Testing

```bash
# Program RAM (requires openFPGALoader)
make ram

# Program Flash (permanent)
make flash

# UART loopback test
# - Send "Hello" via USB-to-UART adapter
# - LED should blink
# - Characters echoed back
```

## References

- [PicoRV32 GitHub](https://github.com/YosysHQ/picorv32)
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
