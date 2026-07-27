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

**Based On**: [Verilog UART IP by Alex Forencich](https://github.com/alexforencich/verilog-uart) – wraps the high-quality `uart.v` core with Wishbone B.4 interface and circular FIFOs for buffering.

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

### 4. `wb_gpio.sv` – LED Output (1-bit GPIO)

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

### 5. `wb_sdram.sv` – 8 MB External SDRAM Controller

**Purpose**: Full-featured SDRAM controller for the M12L64322A chip on Colorlight i9, exposing 8 MB of external DRAM to the CPU via Wishbone B.4.

**Address**: `0x20000000–0x207FFFFF` (8 MB)

**Hardware Notes (Colorlight i9 v7.2 PCB)**:
> The following signals are **hardwired on the PCB** and are NOT driven by the FPGA:
> - `CS_N` → GND (chip always selected)
> - `CKE` → VCC (clock always enabled)
> - `DQM[3:0]` → GND (byte masking permanently disabled)
>
> **Consequence**: all SDRAM reads/writes are always full 32-bit. Individual byte masking via `wb_sel_i` is not supported at the SDRAM level.

**SDRAM Organization**:
| Parameter | Value |
|-----------|-------|
| Chip | M12L64322A |
| Total capacity | 8 MB (64 Mbit) |
| Banks | 4 (`BA[1:0]`) |
| Rows per bank | 2048 (`A[10:0]`, 11-bit) |
| Columns per row | 256 words (`A[7:0]`, 8-bit) |
| Data width | 32 bits (`DQ[31:0]`) |

**Address Mapping** (within 8 MB window):

| `wb_adr_i` bits | SDRAM field | Size |
|-----------------|-------------|------|
| `[22:21]` | Bank `BA[1:0]` | 4 banks |
| `[20:10]` | Row `A[10:0]` | 2048 rows |
| `[9:2]` | Column `A[7:0]` | 256 words |
| `[1:0]` | Byte offset (ignored) | — |

**Parameters**:

```systemverilog
parameter [31:0] ADDR         = 32'h2000_0000;
parameter        CLK_FREQ_MHZ = 25;
parameter        T_INIT_US    = 200;  // Power-on hold delay
parameter        T_RFC_NS     = 63;   // Auto-refresh cycle time
parameter        T_REF_US     = 7;    // Refresh interval (< 7.8 µs)
```

**Timing (25 MHz = 40 ns/cycle)**:

| Parameter | Value | Cycles |
|-----------|-------|--------|
| `tRCD` (RAS→CAS) | 40 ns | 1 |
| `tRP` (precharge) | 40 ns | 1 (auto-precharge) |
| `tRFC` (refresh) | 80 ns | 2 |
| CAS latency | — | 2 SDRAM cycles |
| Refresh period | 7 µs | every 175 cycles |
| Read latency | — | 6 FPGA cycles |
| Write latency | — | 4 FPGA cycles |

**Clock technique**: `sdram_clk = ~clk` (inverted). FPGA outputs settle ~2 ns after `posedge clk`; SDRAM captures them at the next `sdram_clk` rising edge (= `negedge clk`, 20 ns later), giving **~18 ns setup margin** vs. tAS = 2 ns minimum.

**Mode Register**: CL=2, BL=1 (single word burst), sequential — programmed automatically during initialization.

**Initialization sequence** (automatic at power-on/reset):
1. Wait 200 µs (5000 cycles)
2. PRECHARGE ALL banks
3. 2× AUTO REFRESH
4. LOAD MODE REGISTER (CL=2, BL=1)
5. Ready — normal operation begins

**Usage Example**:

```c
#define SDRAM_BASE 0x20000000

volatile uint32_t *sdram = (volatile uint32_t *)SDRAM_BASE;

// Write 32-bit word at offset 0 (bank 0, row 0, col 0)
sdram[0] = 0xDEADBEEF;

// Read it back
uint32_t val = sdram[0];  // Returns 0xDEADBEEF

// Basic memory test
void sdram_test(void) {
    uint32_t i, errors = 0;
    // Write pattern
    for (i = 0; i < 1024; i++) sdram[i] = i ^ 0xA5A5A5A5;
    // Verify
    for (i = 0; i < 1024; i++) {
        if (sdram[i] != (i ^ 0xA5A5A5A5)) errors++;
    }
    // errors == 0 → SDRAM working correctly
}
```

**Limitations**:
- **No byte masking**: `DQM=GND` on PCB → `wb_sel_i` is ignored; always reads/writes full 32 bits
- **Single-word bursts**: BL=1; no burst transfers (can be extended by changing MODE_REG)
- **Auto-precharge**: row closes after every access; no open-row optimization

**SDRAM Physical Pins** (from `colorlight_i9.lpf`):
- `sdram_clk` → B9, `sdram_ras_n` → B10, `sdram_cas_n` → A9, `sdram_we_n` → A10
- `sdram_ba[1:0]` → C8, B11
- `sdram_a[10:0]` → B12, A11..A13, A14, B15, B16, A17, A16, C14, B13
- `sdram_dq[31:0]` → 32 pins (see `colorlight_i9.lpf` for full list)

---

### 6. `wb_i2c.sv` – I2C Master Controller

**Purpose**: Wishbone B.4 (32-bit) wrapper around Alex Forencich's `i2c_master_wbs_8` core, providing a memory-mapped I2C master with hardware FIFOs.

**Based On**: [Verilog I2C IP by Alex Forencich](https://github.com/alexforencich/verilog-i2c) (MIT licence).

**Address**: `0x10005000` (registers at 4-byte stride)

**Register Map**:

| Offset | Name | R/W | Bits |
|--------|------|-----|------|
| 0x00 | STATUS | R/W | `[0]`=busy `[1]`=bus_ctrl `[2]`=bus_act `[3]`=miss_ack (W1C) |
| 0x04 | FIFO_STATUS | R | `[0]`=cmd_empty `[1]`=cmd_full `[3]`=wr_empty `[4]`=wr_full `[6]`=rd_empty `[7]`=rd_full |
| 0x08 | CMD_ADDR | W | 7-bit device address for next command |
| 0x0C | COMMAND | W | `[0]`=start `[1]`=read `[2]`=write `[4]`=stop |
| 0x10 | DATA | R/W | Pop RX FIFO / Push TX FIFO |
| 0x18 | PRESC_LO | W | `prescale[7:0]`  — `prescale = Fclk / (FI2C × 4)` |
| 0x1C | PRESC_HI | W | `prescale[15:8]` — 100 kHz @ 25 MHz → 62, 400 kHz → 15 |

**Parameters**:

```systemverilog
parameter [31:0] ADDR             = 32'h1000_5000;
parameter [15:0] DEFAULT_PRESCALE = 16'd62;    // 100 kHz @ 25 MHz
parameter        CMD_FIFO_DEPTH   = 32;
parameter        WRITE_FIFO_DEPTH = 32;
parameter        READ_FIFO_DEPTH  = 32;
```

**Features**:
- Hardware command, TX, and RX FIFOs (32 entries each)
- Arbitrarily large transfers: CPU stalls on FIFO-full, hardware drains at I2C speed
- Open-drain SCL/SDA with tristate logic (`_t` signals from the i2c_master core)
- `miss_ack` sticky flag if a slave does not acknowledge

**Usage Example** (via `i2c.h` driver):

```cpp
#include "i2c.h"

// Initialize at 400 kHz
i2c0_init(25000000 / (400000 * 4));  // prescale = 15

// Write 2 bytes to 0x3C (SSD1306 command: DISPLAY_ON)
uint8_t buf[] = { 0x00, 0xAF };
i2c0_write(0x3C, buf, 2);

// Read 1 byte from 0x48 (ADS1015)
uint8_t result;
i2c0_read(0x48, &result, 1);
```

**Hardware Pins** (Colorlight i9 pmodg connector, external 4.7 kΩ pullup to 3.3 V required):
- `i2c0_scl` → H4 (pmodg[0])
- `i2c0_sda` → G3 (pmodg[1])

---

### 7. `wb_nodenet.sv` – NodeNet485 Multi-Node RS-485 with MMIO Mailboxes

**Purpose**: Wishbone B.4 slave implementing NodeNet485 over RS-485 @ 1 Mb/s with a mailbox-style register interface.

**Address**: `0x10006000–0x1000601B` (7 registers)

**Important**: The current `wb_nodenet.sv` implementation does **not** access external SDRAM. TX/RX buffering is internal to the module and exposed via MMIO registers.

**Register Map**:

| Offset | Name | R/W | Purpose |
|--------|------|-----|---------|
| 0x00 | TX_CMD | R/W | `[dst(31:24) | len(15:0)]` |
| 0x04 | TX_DATA | R/W | Write payload bytes / read load count |
| 0x08 | RX_HDR | R | `[src(31:24) | rx_valid(16) | len(15:0)]` |
| 0x0C | RX_DATA | R | Read next received payload byte |
| 0x10 | CONFIG | R/W | `[hb_interval(31:10) | prio(9:8) | addr(7:0)]` |
| 0x14 | CONTROL | W | `bit0=trigger_tx bit1=clear_rx bit2=queue_heartbeat` |
| 0x18 | STATUS | R | TX/RX state flags and sticky errors |

**Mailbox Capacity**:
- One staged TX message
- One staged RX message
- Maximum payload length: 2048 bytes

**Hardware Pins**:
- RX: H16 (input from RS-485 transceiver)
- TX: H17 (output to RS-485 transceiver)
- Driver Enable: automatic via transceiver module

For full protocol details, state machines, and firmware API usage, see [README_NODENET.md](README_NODENET.md).

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
