# ESP32 SPI Bridge Notes

This note freezes the initial hardware and firmware choices for the ESP32 sidecar
used with the NodeNet SoC.

## Goals

- keep the FPGA-side RTL small because LUT usage is already high
- use the ESP32 for web configuration and MQTT / Home Assistant integration
- keep NodeNet command semantics between the ESP32 firmware and the PicoRV32 firmware
- use SPI only as a transport layer between the two firmwares

## Selected ESP32 Board

- board family: Wemos D1 mini32 compatible ESP32 module
- PlatformIO board id: `wemos_d1_mini32`
- framework: `espidf`
- preferred IDE: VS Code with PlatformIO

Why this choice:

- PlatformIO is already installed in the current workflow
- ESP-IDF is a better long-term fit than Arduino for SPI transport, web API,
  MQTT, cache management, and persistent configuration
- VS Code + PlatformIO keeps project structure, environments, and dependency
  management under control as the ESP32 firmware grows

## Physical Wiring

Power:

- ESP32 `5V` connected to board `5V`
- ESP32 `GND` connected to board `GND`

Signal voltage:

- all SPI and GPIO signals are `3.3 V`
- never drive any FPGA or ESP32 I/O with `5 V`

### Selected FPGA Port B6 Pins

- `J16` for `spi_sck_i`
- `J18` for `spi_mosi_i`
- `P16` for `spi_miso_o`
- `N4` for `spi_cs_n_i`
- `M3` for `spi_irq_o`
- `T3` reserved for an optional future sideband signal

### Selected ESP32 Pins

- `GPIO18` for `SCK`
- `GPIO23` for `MOSI`
- `GPIO19` for `MISO`
- `GPIO27` for FPGA `CS`
- `GPIO32` for FPGA `IRQ`
- `GPIO33` reserved for an optional future sideband signal

### Added ST7789 Display Pins

- `GPIO5` for display `CS`
- `GPIO16` for display `DC`
- `GPIO17` for display `RST`
- display `BL` tied to `3.3 V`
- display `SCL` shared on `GPIO18`
- display `SDA` shared on `GPIO23`

### Recommended Display Software Stack

- use `esp_lcd` from ESP-IDF for the first ST7789 integration
- keep the display on the existing shared SPI host
- keep the FPGA transport on its own SPI device handle
- let `esp_lcd` own the display-side SPI registration and panel init

### Final Mapping

| Function | ESP32 pin | FPGA pin | Direction |
| --- | --- | --- | --- |
| SPI clock | `GPIO18` | `J16` | ESP32 -> FPGA |
| SPI MOSI | `GPIO23` | `J18` | ESP32 -> FPGA |
| SPI MISO | `GPIO19` | `P16` | FPGA -> ESP32 |
| SPI chip select | `GPIO27` | `N4` | ESP32 -> FPGA |
| IRQ / response ready | `GPIO32` | `M3` | FPGA -> ESP32 |
| Optional sideband | `GPIO33` | `T3` | reserved |

### Shared-Bus Display Mapping

| Function | ESP32 pin | Display pin | Direction |
| --- | --- | --- | --- |
| SPI clock | `GPIO18` | `SCL` | ESP32 -> display |
| SPI MOSI | `GPIO23` | `SDA` | ESP32 -> display |
| Display chip select | `GPIO5` | `CS` | ESP32 -> display |
| Data / command | `GPIO16` | `DC` | ESP32 -> display |
| Display reset | `GPIO17` | `RST` | ESP32 -> display |
| Backlight | `3.3 V` | `BL` | fixed on |

The optional `GPIO33` <-> `T3` line is intentionally not required for the first
bring-up. The initial SPI link should work with `SCK`, `MOSI`, `MISO`, `CS`, and
`IRQ` only.

The ST7789 shares the same SPI bus as the FPGA. Only `SCK` and `MOSI` are shared.
Each slave has its own chip-select, and the FPGA must tri-state `MISO` whenever
its `CS` is inactive.

Keep `GPIO33` <-> `T3` reserved for FPGA-side sideband use. Do not reuse that same
wire for the display `RST`. If a future display sideband is needed, `GPIO33` is a
good candidate for a display input such as `TE`, but only if it stays dedicated on
the ESP32 side rather than shared with the FPGA reserved line.

The current ESP32 firmware bring-up uses a one-shot boot-time ST7789 self-test that
fills the panel with red, green, and blue horizontal bands. This validates shared-bus
registration and basic command/data traffic before adding UI code.

## Proposed LPF Block

This block is intended for the future top-level RTL ports:

- `esp32_spi_sck_i`
- `esp32_spi_mosi_i`
- `esp32_spi_miso_o`
- `esp32_spi_cs_n_i`
- `esp32_spi_irq_o`
- `esp32_spi_sideband_io` optional

Suggested `constraints/colorlight_i9.lpf` snippet:

```lpf
# ESP32 SPI bridge on external port B6
LOCATE COMP "esp32_spi_sck_i" SITE "J16";
IOBUF PORT "esp32_spi_sck_i" IO_TYPE=LVCMOS33;

LOCATE COMP "esp32_spi_mosi_i" SITE "J18";
IOBUF PORT "esp32_spi_mosi_i" IO_TYPE=LVCMOS33;

LOCATE COMP "esp32_spi_miso_o" SITE "P16";
IOBUF PORT "esp32_spi_miso_o" IO_TYPE=LVCMOS33 DRIVE=8;

LOCATE COMP "esp32_spi_cs_n_i" SITE "N4";
IOBUF PORT "esp32_spi_cs_n_i" IO_TYPE=LVCMOS33;

LOCATE COMP "esp32_spi_irq_o" SITE "M3";
IOBUF PORT "esp32_spi_irq_o" IO_TYPE=LVCMOS33 DRIVE=8;

# Optional future sideband line
# LOCATE COMP "esp32_spi_sideband_io" SITE "T3";
# IOBUF PORT "esp32_spi_sideband_io" IO_TYPE=LVCMOS33 DRIVE=8;
```

Notes:

- keep `T3` unused in the first revision
- use `IRQ` as a response-ready signal from FPGA to ESP32
- keep SPI edge mode and sampling fixed in firmware and RTL before increasing the
  clock rate

## Transport Architecture

Selected direction:

- ESP32 is SPI master
- FPGA implements a small SPI slave
- the FPGA SPI block exposes a mailbox or FIFO-style bridge to PicoRV32 firmware
- NodeNet commands remain owned by firmware, not by RTL

This means:

- the FPGA transport block should move framed messages only
- PicoRV32 firmware remains responsible for `pointDefsReq`, `pointStatesReq`,
  writes, and other NodeNet commands
- the ESP32 acts as a NodeNet client specialized for web UI and MQTT

## Minimal FPGA RTL Plan

The first RTL slice should stay strictly in the mailbox domain.

Do not add:

- direct SDRAM access from SPI
- a generic bus master behind SPI
- NodeNet frame parsing in RTL
- JSON parsing in RTL

The FPGA block should only:

- receive SPI bytes from the ESP32 master
- assemble them into one RX mailbox frame buffer
- expose that mailbox to PicoRV32 through Wishbone MMIO
- expose one TX mailbox frame buffer for PicoRV32 responses
- assert an `IRQ` line to the ESP32 when a TX frame is ready

### Proposed RTL Blocks

The minimal decomposition is:

```text
ESP32 SPI master
  |
  v
spi byte shifter / cs framing
  |
  v
rx frame buffer + tx frame buffer + status flags
  |
  v
Wishbone slave mailbox registers
  |
  v
PicoRV32 firmware transport adapter
```

Recommended module split:

- `spi_slave_byte_if.sv`
- `wb_esp32_mailbox.sv`

If you want the smallest first pass, both pieces can live in a single
`wb_esp32_mailbox.sv` module and be separated later only if needed.

### Top-Level Ports

Recommended top-level FPGA ports:

```text
input  wire esp32_spi_sck_i
input  wire esp32_spi_mosi_i
output wire esp32_spi_miso_o
input  wire esp32_spi_cs_n_i
output wire esp32_spi_irq_o
```

The optional `T3` sideband line should stay unused in revision 1.

### Clocking Approach

Keep the first implementation conservative:

- sample the SPI pins into the system clock domain
- run the internal mailbox logic on `sys_clk`
- require a low initial SPI clock such as `1 MHz`

This avoids adding a second real-time clock domain design in the first pass.

At `25 MHz` system clock and `1 MHz` SPI, oversampling is comfortable enough for
an initial bring-up if the slave state machine is kept simple.

### SPI Operating Mode

Freeze the first revision to:

- `CPOL = 0`
- `CPHA = 0`
- `MSB first`

Do not make SPI mode configurable in RTL yet.

## Proposed Wishbone Mailbox Map

Use a small, explicit register map similar in style to the current mailbox-based
peripherals.

Suggested base address:

- `0x10009000`

Suggested name:

- `ESP32_SPI_BASE`

Suggested register map:

| Offset | Name | R/W | Purpose |
| --- | --- | --- | --- |
| `0x00` | `STATUS` | R | mailbox state and sticky error bits |
| `0x04` | `CONTROL` | W | clear RX, arm TX, clear IRQ, soft reset |
| `0x08` | `RX_LEN` | R | received frame length in bytes |
| `0x0C` | `RX_DATA` | R | pop next RX byte |
| `0x10` | `TX_LEN` | R/W | response frame length in bytes |
| `0x14` | `TX_DATA` | W | push next TX byte |
| `0x18` | `IRQ_CTRL` | R/W | enable bits for response-ready / error IRQ |
| `0x1C` | `DEBUG` | R | live transport state for bring-up |

### STATUS Bits

Suggested `STATUS` layout:

- bit `0`: `rx_ready`
- bit `1`: `rx_overflow`
- bit `2`: `rx_frame_error`
- bit `3`: `tx_ready_for_cpu`
- bit `4`: `tx_loaded`
- bit `5`: `tx_ready_for_esp32`
- bit `6`: `irq_asserted`
- bit `7`: `spi_active`
- bit `8`: `rx_in_progress`
- bit `9`: `tx_in_progress`

Minimal semantics:

- `rx_ready` means one complete frame is waiting for PicoRV32
- `tx_ready_for_cpu` means PicoRV32 may write a new response
- `tx_ready_for_esp32` means the ESP32 may fetch a prepared response
- `irq_asserted` mirrors the physical `esp32_spi_irq_o`

### CONTROL Bits

Suggested `CONTROL` layout:

- bit `0`: `clear_rx`
- bit `1`: `commit_tx`
- bit `2`: `clear_irq`
- bit `3`: `soft_reset`

Semantics:

- `clear_rx` releases the RX mailbox after PicoRV32 consumed the frame
- `commit_tx` marks the TX mailbox valid and raises `IRQ`
- `clear_irq` deasserts `IRQ` after the ESP32 has fetched the response
- `soft_reset` clears mailbox state and sticky errors

### Why Byte-Wide MMIO

For the first slice, byte-push / byte-pop is the lowest-risk design because:

- the SPI link is byte-oriented anyway
- firmware code stays straightforward
- it avoids packing edge cases on partial words
- register behavior is easy to observe during bring-up

If throughput becomes an issue later, `RX_DATA32` / `TX_DATA32` registers can be
added as a second-step optimization.

## SPI Transaction Model

The cleanest first transaction model is command-oriented rather than fully
streaming.

Suggested SPI opcodes:

- `0x01` = read `STATUS`
- `0x02` = write request frame bytes into RX mailbox
- `0x03` = read response frame bytes from TX mailbox
- `0x04` = write `CONTROL`

### ESP32 to FPGA Request Write

1. ESP32 lowers `CS`
2. ESP32 sends opcode `0x02`
3. ESP32 sends frame length low byte then high byte
4. ESP32 sends frame payload bytes
5. FPGA validates mailbox capacity and sets `rx_ready`
6. ESP32 raises `CS`

### ESP32 Response Fetch

1. ESP32 sees `IRQ` high or polls `STATUS`
2. ESP32 lowers `CS`
3. ESP32 sends opcode `0x03`
4. FPGA returns response length then response bytes on `MISO`
5. ESP32 raises `CS`
6. ESP32 sends `CONTROL.clear_irq`

This is intentionally not a generic register-read SPI protocol yet. It is a
small framed mailbox protocol with only a few control opcodes.

## Minimal Buffer Sizing

Recommended first-pass sizes:

- RX mailbox: `512` bytes
- TX mailbox: `512` bytes

Why `512` bytes:

- large enough for initial `whoisReq`, `pointDefsReq`, `pointStatesReq` pages
- small enough to stay cheap in LUT/FF terms
- easy to grow later if the JSON payloads prove too large

If JSON responses grow beyond that, prefer pagination first before increasing the
hardware buffer sizes aggressively.

## PicoRV32 Transport Adapter Plan

The firmware-side adapter should do only three jobs:

1. poll or IRQ-check the mailbox `STATUS`
2. when `rx_ready=1`, read the request frame bytes into a local buffer
3. build a response frame and commit it back through `TX_LEN`, `TX_DATA`, and
   `CONTROL.commit_tx`

The adapter should present NodeNetCore with:

- logical source endpoint `255`
- transport kind `spi_host`
- payload bytes decoded from the SPI frame

When NodeNetCore emits a reply to logical destination `255`, the adapter should
route it back to the SPI mailbox instead of `wb_nodenet`.

## First Bring-Up Validation Order

Keep validation extremely narrow:

1. `STATUS` read works over SPI
2. ESP32 writes a dummy frame into RX mailbox
3. PicoRV32 sees `rx_ready` and reads the exact byte count back through MMIO
4. PicoRV32 writes a dummy response frame
5. FPGA asserts `IRQ`
6. ESP32 reads the exact response bytes back
7. only then wire the mailbox to `whoisReq`

This keeps the first test completely independent from NodeNet business logic.

## NodeNet Addressing Convention

The SPI link should reuse the existing reserved-address convention already used
by the desktop bridge.

Selected rule:

- `0` means NodeNet broadcast on the real bus
- `1..254` mean regular NodeNet node addresses
- `255` is a reserved host endpoint behind a local management transport

For this project, `255` should be treated as a logical endpoint class, not as a
real NodeNet node physically present on the RS485 bus.

That means the ESP32 can identify itself as `255` when it exchanges NodeNet-style
messages with PicoRV32 over SPI, while still remaining outside the real NodeNet
address space used on the field bus.

### Why Reuse 255

- it matches the existing desktop-driver convention
- it avoids inventing a second host identity model
- it keeps request and response payloads compatible with existing NodeNet JSON
  examples

### Routing Rule

The important rule is that destination `255` must not be treated as broadcast.

Instead, it means the message is addressed to the local host-facing transport.

Recommended transport routing:

| Logical destination | Meaning | Routed to |
| --- | --- | --- |
| `0` | NodeNet broadcast | NodeNet RS485 transport |
| `1..254` | real NodeNet node | NodeNet RS485 transport |
| `255` | local host endpoint | SPI mailbox transport |

### Firmware Interpretation

For SPI-originated requests:

- the ESP32 may send payloads with `from = 255`
- PicoRV32 should preserve that logical source when building the request context
- any response addressed to `255` should be returned to the SPI mailbox
- responses addressed to `1..254` or `0` may be emitted on the real NodeNet bus
  when a command explicitly requires that behavior

This is slightly stricter than the current historical shortcut where some paths
map `255` to `0` for reply emission. For the ESP32 bridge, prefer explicit
transport-aware routing over `255 -> broadcast` fallback.

## NodeNet Over SPI Framing

The initial SPI link should preserve NodeNet command semantics and add a small
binary framing layer for transport reliability.

### Phase 1 Principles

- one request in flight at a time
- ESP32 always initiates clocking as SPI master
- the FPGA transport block does not parse NodeNet payload content
- PicoRV32 firmware owns request decoding and response generation
- initial payload format can stay JSON for fast integration

### Message Classes

- `request`: ESP32 to PicoRV32
- `response`: PicoRV32 to ESP32
- `event`: PicoRV32 to ESP32, fetched by the ESP32 after `IRQ` assertion

### Frame Format V1

All multi-byte fields are little-endian.

```text
offset  size  field
0       2     magic = 0x4E53  ('N''S')
2       1     version = 1
3       1     frame_type
4       2     sequence
6       2     payload_len
8       2     flags
10      N     payload bytes
10+N    2     crc16_ccitt over header+payload without the crc field
```

Suggested `frame_type` values:

- `1` = request
- `2` = response
- `3` = event
- `4` = error

Suggested `flags` bits:

- bit `0`: more fragments follow
- bit `1`: payload is UTF-8 JSON
- bit `2`: payload is binary
- bit `3`: response required

### Initial Payload Contract

For the first bring-up, keep the existing NodeNet request bodies and responses as
UTF-8 JSON payloads inside the SPI frame.

For host-originated messages over SPI, use the same convention as the existing
desktop bridge and set `from` to `255`.

Examples of first commands to support:

- `whoisReq`
- `pointDefsReq`
- `pointStatesReq`
- a narrow point write request once reads are validated

Example request payload:

```json
{"cmd":"pointDefsReq","from":255,"path":"","offset":0,"limit":8}
```

Example response payload:

```json
{"cmd":"pointDefsRes","to":255,"path":"","offset":0,"count":8,"total":42,"hasMore":true}
```

### Mailbox Behavior

Recommended mailbox behavior between FPGA and PicoRV32:

- FPGA receives one full SPI frame into an RX buffer
- FPGA exposes RX-ready status to PicoRV32
- PicoRV32 consumes the frame, builds a response, and writes a TX buffer
- FPGA raises `IRQ` to the ESP32 when a TX frame is ready
- ESP32 performs a read transaction to fetch the response frame

### Bring-Up Sequence

1. ESP32 resets and configures SPI at `1 MHz`
2. ESP32 polls a small transport status register until the FPGA mailbox is ready
3. ESP32 sends a framed `whoisReq`
4. PicoRV32 processes it through the NodeNet stack
5. FPGA asserts `IRQ` when the response frame is available
6. ESP32 reads the framed response and validates the CRC
7. Only after that should `pointDefsReq` and `pointStatesReq` be enabled

### Why Keep JSON First

- fastest path to reuse the current NodeNet command surface
- easiest way to compare ESP32 results with the existing desktop client
- lower RTL complexity because only transport framing is new

Binary payload compaction can be introduced later for high-frequency read paths.

## PlatformIO Starting Point

Recommended initial `platformio.ini` environment:

```ini
[env:wemos_d1_mini32]
platform = espressif32
board = wemos_d1_mini32
framework = espidf
monitor_speed = 115200
upload_speed = 921600
```

## Initial ESP32 Firmware Scope

The first ESP32 milestone should stay narrow:

- bring up SPI master transport
- exchange framed request / response messages with the FPGA mailbox
- issue NodeNet-style requests for `pointDefsReq` and `pointStatesReq`
- validate a small local cache of point definitions and point states

The web UI and MQTT layers should come after the SPI transport is stable.

## Notes For First Bring-Up

- start with a low SPI clock, for example `1 MHz`
- use the FPGA `IRQ` line to signal response-ready instead of aggressive polling
- keep only one request in flight for the first protocol revision
- reserve the optional sideband line until a real need appears
- if signal integrity is marginal, add small series resistors on `SCK`, `MOSI`,
  and `CS`

## Deferred Work

Not part of the first hardware freeze:

- direct SDRAM access from the ESP32
- a generic memory bus exposed over SPI
- binary compaction of NodeNet payloads
- Home Assistant discovery payload design
- final REST / WebSocket API structure on the ESP32