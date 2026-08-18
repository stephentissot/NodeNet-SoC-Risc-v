# Wishbone Bus Peripherals

This directory contains reusable Wishbone B.4-compliant peripheral modules for the Colorlight i9 SoC.

## Overview

All modules follow the Wishbone B.4 standard with:
- 32-bit address bus
- 32-bit data bus
- Single-cycle read/write (in most cases)
- Byte-enable support (`wb_sel_i`)

## Modules

### 1. `wb_uart.sv` – Wishbone UART Wrapper + FIFOs

**Purpose**: Memory-mapped UART peripheral for CPU firmware (`DATA`, `STATUS`, `BAUD`) with integrated RX/TX FIFOs.

**Architecture**:
- `uart_simple.sv` = reusable UART 8N1 serial core
- `wb_uart.sv` = Wishbone B.4 wrapper + register map + RX/TX FIFOs around `uart_simple`

This matches the same layered approach used by NodeNet:
- `wb_nodenet.sv` keeps its own NodeNet framing/mailbox logic and also uses `uart_simple` for wire-level UART.

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

**Baud Rate Calculation** (`REG_BAUD`):
```
baud = clock_frequency / (8 * prescale)
prescale = clock_frequency / (8 * baud)

Example: 115200 baud @ 25 MHz
prescale = 25_000_000 / (8 * 115200) ≈ 27.1 → use 27
```

**Features**:
- **16-byte RX FIFO**: Prevents data loss during CPU delays
- **16-byte TX FIFO**: Allows burst writes, hardware handles serial transmission
- **Circular buffer logic**: Automatic wrap-around with full/empty detection
- **Error flags**: Sticky bits for over-run and frame errors (sticky until cleared)

**Usage Example**:

```c
#define UART1_DATA     0x10004000
#define UART1_STATUS   0x10004004
#define UART1_BAUD     0x10004008

volatile uint32_t *uart_data   = (volatile uint32_t *)UART1_DATA;
volatile uint32_t *uart_status = (volatile uint32_t *)UART1_STATUS;
volatile uint32_t *uart_baud   = (volatile uint32_t *)UART1_BAUD;

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
- Direct UART RX/TX pins (`rxd`, `txd`) for the `UART1` peripheral instance
- Wishbone access is edge-fired (`wb_fire`) to avoid duplicated reads/writes when `cyc/stb` are held
- FIFOs absorb software latency and burst writes

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
0x00010000  runtime RAM base
0x00010XXX  initialized data / small runtime state
0x0001FFFF  stack top (grows down)
```

**Notes**:
- Byte-enable is critical for byte/half-word writes
- On read: full 32-bit word returned; firmware masks as needed

---

### 3. `wb_rom.sv` – 64 KiB Stage0 Boot ROM

**Purpose**: Immutable stage0 bootloader storage, loaded from synthesis-time hex file.

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
- Contains the ROM-resident `boot_stage0` image that validates the SPI-flash application image and jumps to SDRAM execution

**Hex File Format** (Intel HEX or Verilog):
```
:10000000 AABBCCDD EEFF0011 22334455 ...
:00000001FF
```

**Usage**:
- PicoRV32 CPU boots from `pc=0x00000000`
- First instruction of `boot_stage0` must be in this ROM
- No runtime modification (for reliability)

**Current Implementation**:
- Stage0 ROM image built to `src/firmware/build/boot_stage0.hex`
- Referenced in [wb_rom.sv](wb_rom.sv) at synthesis time
- Contains the stage0 boot path only; the main runtime application is packaged separately in SPI flash and executes from SDRAM

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

### 5. `led_pulse_core.sv` – Generic LED Pulse Engine (No Bus)

**Purpose**: Reusable hardware pulse/state engine used by bus wrappers and event-driven blocks.

**Inputs**:
- `trigger_i`: starts one non-blocking pulse
- `set_default_i`: updates persistent default state
- `default_value_i`: default state value when `set_default_i=1`
- `blink_cycles_i`: pulse width in clock cycles (`0` uses module parameter)

**Polarity**:
- `ACTIVE_LOW=0`: `led_o=1` means LED ON (active-high)
- `ACTIVE_LOW=1`: `led_o=0` means LED ON (active-low)

**Outputs**:
- `led_o`: current LED level
- `busy_o`: pulse currently active
- `default_state_o`: latched default state

This module is used by both `wb_led.sv` (Wishbone wrapper) and `wb_nodenet.sv` (event-driven TX/RX activity LEDs).

---

### 6. `wb_led.sv` – Non-Blocking LED Pulse Controller

**Purpose**: Hardware-timed one-shot LED pulses over Wishbone, used for RJ45 LEDs.

**Addresses in current top-level integration**:
- `0x10000004`: LED0 (pin F5)
- `0x10000008`: LED1 (pin E6)

**Write Commands** (`wb_dat_i`):
- `bit0 = 1`: trigger one-shot pulse
- `bits31:3`: optional duration override (clock cycles, `0` means use `BLINK_CYCLES` parameter)
- `bit1 = 1`: update default state
- `bit2`: new default state value (0 = OFF, 1 = ON)

**Readback** (`wb_dat_o`):
- `bit0`: current LED output level
- `bit1`: pulse active (`busy`)
- `bit2`: default state

**Parameters**:

```systemverilog
parameter ADDR = 32'h1000_0004;
parameter ACTIVE_LOW = 1'b0;
parameter DEFAULT_STATE = 1'b0;
parameter BLINK_CYCLES = 32'd2500000; // 100 ms @ 25 MHz
```

**Firmware usage**:
- See `src/firmware/include/led.h` for helper functions and `wb_led::Led` wrapper.
- Current policy: D2 (`wb_gpio`) is used as the primary boot/status indicator; RJ45 LEDs can be driven through `wb_led` when needed.
- Current board policy for external LEDs is active-low with pull-up, so wrappers set `ACTIVE_LOW=1` in top-level integrations.

---

### 7. `wb_sdram_litedram.sv` – 8 MB External SDRAM Window via LiteDRAM

**Purpose**: Wishbone-facing SDRAM wrapper for the M12L64322A chip on Colorlight i9, exposing 8 MB of external DRAM to the CPU through the generated LiteDRAM core.

**Current role in the boot flow**:
- Stage0 copies the packaged application image from SPI flash into this SDRAM window.
- After the copy and CRC checks, the CPU jumps to the application entry point in SDRAM.
- Runtime `SDRAM_DATA` objects and SDRAM self-tests share this same window, so destructive tests must stay inside reserved scratch space.

**Address**: `0x20000000–0x207FFFFF` (8 MB)

**Hardware Notes (Colorlight i9 v7.2 PCB)**:
> The following signals are **hardwired on the PCB** and are NOT driven by the FPGA:
> - `CS_N` → GND (chip always selected)
> - `CKE` → VCC (clock always enabled)
> - `DQM[3:0]` → GND (byte masking permanently disabled)
>
> **Consequence**: the SDRAM chip itself always sees full 32-bit accesses. The LiteDRAM-facing wrapper compensates for sub-word CPU writes with an internal read-modify-write sequence when `wb_sel_i != 4'b1111`.

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
| `[1:0]` | Byte offset inside 32-bit word | handled in Wishbone wrapper |

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

// Warning: the runtime application itself executes from this SDRAM window.
// Direct raw accesses are only safe when you control placement.

// Safe pattern: use linker-placed buffers or a reserved scratch area.
extern volatile uint32_t g_sdram_test_scratch_words[];
g_sdram_test_scratch_words[0] = 0xDEADBEEF;

// Read it back
uint32_t val = g_sdram_test_scratch_words[0];

// Basic scratch-area memory test
void sdram_test(void) {
    uint32_t i, errors = 0;
    // Write pattern
    for (i = 0; i < 1024; i++) g_sdram_test_scratch_words[i] = i ^ 0xA5A5A5A5;
    // Verify
    for (i = 0; i < 1024; i++) {
        if (g_sdram_test_scratch_words[i] != (i ^ 0xA5A5A5A5)) errors++;
    }
    // errors == 0 -> scratch area is behaving correctly without corrupting the runtime image
}
```

**Limitations**:
- **No native SDRAM byte masking**: `DQM=GND` on PCB, so sub-word writes are emulated in `wb_sdram_litedram` with read-modify-write cycles instead of true masked writes
- **Single-word bursts**: BL=1; no burst transfers (can be extended by changing MODE_REG)
- **Auto-precharge**: row closes after every access; no open-row optimization

**SDRAM Physical Pins** (from `colorlight_i9.lpf`):
- `sdram_clk` → B9, `sdram_ras_n` → B10, `sdram_cas_n` → A9, `sdram_we_n` → A10
- `sdram_ba[1:0]` → C8, B11
- `sdram_a[10:0]` → B12, A11..A13, A14, B15, B16, A17, A16, C14, B13
- `sdram_dq[31:0]` → 32 pins (see `colorlight_i9.lpf` for full list)

---

### 6. `wb_i2c.sv` – I2C Master Controller

**Purpose**: Wishbone B.4 (32-bit) wrapper around Alex Forencich's `i2c_master_wbs_16` core, providing a memory-mapped I2C master with hardware FIFOs.

**Based On**: [Verilog I2C IP by Alex Forencich](https://github.com/alexforencich/verilog-i2c) (MIT licence).

**Address**: `0x10005000` (registers at 4-byte stride)

**Register Map**:

| Offset | Name | R/W | Bits |
|--------|------|-----|------|
| 0x00 | STATUS | R/W | `[0]`=busy `[1]`=bus_ctrl `[2]`=bus_act `[3]`=miss_ack (W1C), `[15:8]` FIFO summary |
| 0x04 | COMMAND | R/W | `[6:0]`=addr `[8]`=start `[9]`=read `[10]`=write `[11]`=write_mult `[12]`=stop |
| 0x08 | DATA | R/W | `[7:0]`=data `[8]`=data_valid `[9]`=data_last |
| 0x0C | PRESCALE | R/W | `prescale[15:0]`, with `prescale = Fclk / (FI2C × 4)` |

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

I2c wire;

// Initialize at 400 kHz
wire.begin(25000000 / (400000 * 4));  // prescale = 15

// Write 2 bytes to 0x3C (SSD1306 command: DISPLAY_ON)
uint8_t buf[] = { 0x00, 0xAF };
wire.write(0x3C, buf, 2);
```

**Hardware Pins** (Colorlight i9, external 4.7 kΩ pullup to 3.3 V required):
- `i2c0_scl` → D18
- `i2c0_sda` → D17

---

### 7. `wb_nodenet.sv` – NodeNet485 Multi-Node RS-485 with MMIO Mailboxes

**Purpose**: Wishbone B.4 slave implementing NodeNet485 over RS-485 @ 1 Mb/s with a mailbox-style register interface.

**Address**: `0x10006000–0x1000601B` (7 registers)

**Important**: The current `wb_nodenet.sv` implementation does **not** access external SDRAM. TX/RX buffering is internal to the module and exposed via MMIO registers.

**UART Layer**:
- `wb_nodenet` uses `uart_simple` directly for byte-level UART RX/TX.
- `wb_nodenet` does not depend on `wb_uart`.

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
- RX: G5 (input from RS-485 transceiver)
- TX: D16 (output to RS-485 transceiver)
- Driver Enable: automatic via transceiver module

For full protocol details, state machines, and firmware API usage, see [README_NODENET.md](README_NODENET.md).

---

## Wishbone Interconnect Integration

All modules are instantiated in [../top.sv](../top.sv) with address decoding:

```systemverilog
// Address ranges
always_comb begin
    wb_rom_sel    = wb_cyc && wb_stb && (wb_adr[31:16] == 16'h0000);
    wb_ram_sel    = wb_cyc && wb_stb && (wb_adr[31:16] == 16'h0001);
    wb_led_d2_sel = wb_cyc && wb_stb && (wb_adr == 32'h1000_0000);
    wb_led0_sel   = wb_cyc && wb_stb && (wb_adr == 32'h1000_0004);
    wb_led1_sel   = wb_cyc && wb_stb && (wb_adr == 32'h1000_0008);
    wb_uart1_sel  = wb_cyc && wb_stb && (wb_adr[31:12] == 20'h10004);
    wb_i2c0_sel   = wb_cyc && wb_stb && (wb_adr[31:12] == 20'h10005);
    wb_nodenet_sel = wb_cyc && wb_stb && (wb_adr[31:12] == 20'h10006);
    wb_flash_sel  = wb_cyc && wb_stb && (wb_adr[31:12] == 20'h10007);
    wb_sdram_sel  = wb_cyc && wb_stb && (wb_adr[31:23] == 9'h040);
end

// Data multiplexer
assign wb_dat_i = rom_ack     ? rom_dat     :
                  ram_ack     ? ram_dat     :
                  nodenet_ack ? nodenet_dat :
                  i2c0_ack    ? i2c0_dat    :
                  flash_ack   ? flash_dat   :
                  led_d2_ack  ? led_d2_dat  :
                  led0_ack    ? led0_dat    :
                  led1_ack    ? led1_dat    :
                                    uart1_ack   ? uart1_dat   :
                  sdram_ack   ? sdram_dat   :
                  32'h0;

// Ack multiplexer
assign wb_ack = rom_ack | ram_ack | led_d2_ack | led0_ack | led1_ack |
                                uart1_ack | nodenet_ack | i2c0_ack | flash_ack | sdram_ack;
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
