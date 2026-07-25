# Wishbone Bus Peripherals

This directory contains reusable Wishbone B.4-compliant peripheral modules for the Colorlight i9 SoC.

## Overview

All modules follow the Wishbone B.4 standard with:
- 32-bit address bus
- 32-bit data bus
- Single-cycle read/write (in most cases)
- Byte-enable support (`wb_sel_i`)

## Modules

### 1. `wb_uart.sv` – UART with Integrated FIFOs

**Purpose**: Serial communication with RS485 driver support, buffered I/O.

**Address**: Configurable (default: `0x10001000`)

**Register Map**:

| Offset | Name | Read | Write | Bits | Notes |
|--------|------|------|-------|------|-------|
| 0x0 | DATA | RX FIFO pop | TX FIFO push | [7:0] | 8-bit characters |
| 0x4 | STATUS | Flags (RO) | N/A | [5:0] | See below |
| 0x8 | BAUD | N/A | Prescale | [15:0] | Default 27 (~115.2k @ 25 MHz) |

**STATUS Register** (read-only):

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | RX_FIFO_EMPTY | 1 = no data in RX FIFO, cannot read DATA |
| 1 | RX_FIFO_FULL | 1 = RX FIFO full, next byte will be dropped |
| 2 | TX_FIFO_EMPTY | 1 = TX FIFO empty, no pending transmissions |
| 3 | TX_FIFO_FULL | 1 = TX FIFO full, cannot write DATA |
| 4 | RX_OVERRUN_STICKY | 1 = RX overflow occurred (write 1 to clear) |
| 5 | RX_FRAMEERR_STICKY | 1 = framing error occurred (write 1 to clear) |

**Parameters**:

```systemverilog
parameter ADDR = 32'h1000_1000;        // Base address
parameter DEFAULT_PRESCALE = 16'd27;   // Baud rate divisor
parameter FIFO_ADDR_WIDTH = 4;         // FIFO depth: 2^4 = 16 bytes
```

**Baud Rate Calculation**:
```
baud = clock_frequency / (16 * prescale)
prescale = clock_frequency / (16 * baud)

Example: 115200 baud @ 25 MHz
prescale = 25_000_000 / (16 * 115200) ≈ 13.5 → use 14 (actual: 111328 baud)
prescale = 27 gives ~115200 baud (actually 91551 baud with 27)
```

**Features**:
- **16-byte RX FIFO**: Prevents data loss during CPU delays
- **16-byte TX FIFO**: Allows burst writes, hardware handles serial transmission
- **Circular buffer logic**: Automatic wrap-around with full/empty detection
- **Error flags**: Sticky bits for over-run and frame errors (sticky until cleared)

**Usage Example**:

```c
#define UART0_DATA     0x10001000
#define UART0_STATUS   0x10001004
#define UART0_BAUD     0x10001008

volatile uint32_t *uart_data   = (volatile uint32_t *)UART0_DATA;
volatile uint32_t *uart_status = (volatile uint32_t *)UART0_STATUS;
volatile uint32_t *uart_baud   = (volatile uint32_t *)UART0_BAUD;

// Initialize
void uart_init(uint16_t prescale) {
    *uart_baud = prescale;  // Set baud rate
}

// Transmit byte (blocking)
void uart_putc(char c) {
    while (*uart_status & 0x8);  // Wait for TX not full
    *uart_data = c;
}

// Receive byte (blocking)
char uart_getc(void) {
    while (*uart_status & 0x1);  // Wait for RX not empty
    return (char)(*uart_data & 0xFF);
}

// Non-blocking receive
int uart_getc_nb(char *c) {
    if (*uart_status & 0x1) return -1;  // No data
    *c = (char)(*uart_data & 0xFF);
    return 0;
}
```

**Hardware Integration**:
- **TX** line drives RS485 TX_EN when FIFO not empty
- **RX** line connected directly to receiver input
- FIFO prevents overrun on moderate interrupt latency

---

### 2. `wb_ram.sv` – 64 KiB Embedded RAM

**Purpose**: Main system memory for stack and variables.

**Address**: `0x00010000–0x0002FFFF` (64 KiB, fixed)

**Register Map**: Linear 32-bit word array (no special registers)

**Parameters**:

```systemverilog
parameter ADDR_WIDTH = 14;  // 2^14 = 16K words = 64K bytes
parameter ADDR = 32'h0001_0000;
```

**Features**:
- Single-port synchronous RAM
- 32-bit data width with byte-enable support
- Sub-word writes via `wb_sel_i` (4 enable bits for [3:0] bytes)
- Single-cycle RW latency

**Usage Example**:

```c
volatile uint32_t *ram_base = (volatile uint32_t *)0x00010000;

// Write 32-bit word
ram_base[0x100] = 0xDEADBEEF;

// Read 32-bit word
uint32_t data = ram_base[0x100];

// Via byte pointer (sub-word access)
volatile uint8_t *ram_bytes = (volatile uint8_t *)0x00010000;
ram_bytes[0x402] = 0x42;  // Write single byte at offset 0x402
```

**Memory Layout** (from linker script):
```
0x00010000  .text (code, from ROM copy)
0x00010XXX  .data (initialized data)
0x00020000  .bss (uninitialized globals, stack grows down)
```

**Notes**:
- Byte-enable is critical for byte/half-word writes
- On read: full 32-bit word returned; firmware masks as needed

---

### 3. `wb_rom.sv` – 64 KiB Boot ROM

**Purpose**: Immutable boot code storage, loaded from synthesis-time hex file.

**Address**: `0x00000000–0x0000FFFF` (64 KiB, fixed)

**Register Map**: Read-only 32-bit word array

**Parameters**:

```systemverilog
parameter ADDR_WIDTH = 14;  // 2^14 = 16K words = 64K bytes
parameter INIT_FILE = "firmware.hex";  // Yosys $readmemh() format
parameter ADDR = 32'h0000_0000;
```

**Features**:
- Read-only (writes ignored, no error signaling)
- Single-cycle read latency
- Initialized at synthesis from `.hex` file
- Typically contains bootstrap code + linker script .text section

**Hex File Format** (Intel HEX or Verilog):
```
:10000000 AABBCCDD EEFF0011 22334455 ...
:00000001FF
```

**Usage**:
- PicoRV32 CPU boots from `pc=0x00000000`
- First instruction must be in this ROM
- No runtime modification (for reliability)

**Current Implementation**:
- Firmware built to `src/firmware/build/blink.hex`
- Referenced in [wb_rom.sv](wb_rom.sv) at synthesis time
- Firmware includes boot code (linker script `.text` section)

---

### 4. `simpleGPIO.sv` – LED Output (1-bit GPIO)

**Purpose**: Simple output control for LED indicator (D2 on Colorlight i9).

**Address**: `0x10000000` (fixed)

**Register Map**:

| Offset | Name | Width | Bits | Notes |
|--------|------|-------|------|-------|
| 0x0 | LED_OUT | 32-bit | [0] | Only bit 0 used; others ignored |

**Parameters**:

```systemverilog
parameter ADDR = 32'h1000_0000;
parameter OUTPUT_PIN = "D2";  // FPGA port name
```

**Features**:
- Single write-only register
- Only byte [0] is latched (see `wb_sel_i[0]`)
- Output combinatorial (reflects register immediately)
- Read returns last written value

**Usage Example**:

```c
#define LED_GPIO 0x10000000

volatile uint32_t *led_gpio = (volatile uint32_t *)LED_GPIO;

// Turn on LED
*led_gpio = 1;

// Turn off LED
*led_gpio = 0;

// Toggle
uint32_t state = *led_gpio;
*led_gpio = !state;
```

**Hardware**:
- FPGA port: `output wire led_out`
- Mapped to physical pin D2 via constraints file (`colorlight_i9.lpf`)

---

## Wishbone Interconnect Integration

All modules are instantiated in [../top.sv](../top.sv) with address decoding:

```systemverilog
// Address ranges
always_comb begin
    wb_rom_sel  = (wb_adr_i[31:16] == 16'h0000);
    wb_ram_sel  = (wb_adr_i[31:16] == 16'h0001);
    wb_uart0_sel = (wb_adr_i[31:12] == 20'h1000_1);
    wb_led_sel  = (wb_adr_i[31:16] == 16'h1000);
end

// Data multiplexer
assign wb_dat_i = wb_rom_sel  ? wb_rom_data :
                  wb_ram_sel  ? wb_ram_data :
                  wb_uart0_sel ? wb_uart0_data :
                  wb_led_sel  ? wb_led_data : 32'h0;

// Ack multiplexer
assign wb_ack = wb_rom_sel  ? wb_rom_ack :
                wb_ram_sel  ? wb_ram_ack :
                wb_uart0_sel ? wb_uart0_ack :
                wb_led_sel  ? wb_led_ack : 1'b0;
```

---

## Design Patterns

### Full/Empty Detection (FIFO)

```systemverilog
wire fifo_empty = (read_ptr == write_ptr);
wire fifo_full  = ((write_ptr + 1) == read_ptr);
```

### Circular Pointer Wrap

```systemverilog
wire [ADDR_WIDTH:0] next_write_ptr = write_ptr + 1;
wire [ADDR_WIDTH:0] next_read_ptr  = read_ptr + 1;
// Auto-wrap on overflow (ADDR_WIDTH+1 bits allows MSB wrap)
```

### Byte-Enable Masking

```systemverilog
wire [31:0] write_data_masked = {
    wb_sel_i[3] ? wb_dat_o[31:24] : mem[addr][31:24],
    wb_sel_i[2] ? wb_dat_o[23:16] : mem[addr][23:16],
    wb_sel_i[1] ? wb_dat_o[15:8]  : mem[addr][15:8],
    wb_sel_i[0] ? wb_dat_o[7:0]   : mem[addr][7:0]
};
```

---

## Extending the SoC

To add a new Wishbone peripheral:

1. **Create module** (`wb_mydevice.sv`) with standard Wishbone interface
2. **Assign address range** in [../top.sv](../top.sv) address decoder
3. **Instantiate** the module with appropriate address and parameters
4. **Add data/ack** to multiplexers
5. **Update documentation** (this file) and firmware headers

Example:

```systemverilog
// New SPI device at 0x1000_2000
parameter SPI_ADDR = 32'h1000_2000;

wb_spi spi_inst (
    .clk(clk),
    .rst(rst),
    .wb_adr_i(wb_adr_i[3:0]),  // Decode lower bits
    .wb_dat_o(wb_spi_data),
    .wb_dat_i(wb_dat_o),
    .wb_we_i(wb_we_i),
    .wb_ack_o(wb_spi_ack),
    .wb_sel_i(wb_sel_i)
);
```

---

## References

- [Wishbone B.4 Specification](https://cdn.opencores.org/downloads/wbspec_b4.pdf)
- [RISC-V Privileged Architecture](https://riscv.org/specifications/)
- [SystemVerilog Language Reference](https://ieee1800.org/)
