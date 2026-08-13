# SPI Flash (W25Q64) Module — Low-Level Driver + FlashDB KV

## Overview

The W25Q64JVSIQ is an 8 MB SPI flash memory IC on the Colorlight i9.

Current firmware integration has two layers:
- **Low-level layer**: `Flash` class (`src/firmware/lib/flash/flash.h`) for page read/write and sector erase.
- **KV layer**: FlashDB (`flashdb_port.cpp`) on top of a dedicated FAL partition.

### Specifications

| Parameter | Value |
|-----------|-------|
| Capacity | 8 MB (67,108,864 bytes) |
| Page size | 256 bytes (program unit) |
| Sector size | 4 KB (erase unit) |
| Block size | 64 KB (erase unit) |
| SPI speed | Up to 50 MHz (we use 10 MHz for safety) |
| Voltage | 2.7–3.6 V (3.3 V typical) |
| Address width | 24-bit (0x000000–0x7FFFFF) |

## Hardware Integration

### Pins

| Signal | FPGA Pin | Flash Pin | Purpose |
|--------|----------|-----------|---------|
| CLK | Dedicated USRMCLK path (not GPIO) | 6 (CLK) | SPI clock |
| MOSI | W2 | 5 (DI) | Master out, slave in |
| MISO | V2 | 2 (DO) | Master in, slave out |
| CS_N | R2 | 1 (CS) | Chip select (active low) |
| VCC | 3.3 V | 8 (VCC) | Power supply |
| GND | GND | 4 (GND) | Ground |

> **Note**: Pin assignments are already present in `constraints/colorlight_i9.lpf` for `flash_cs_n`, `flash_miso`, and `flash_mosi`.
> `CLK` is routed through the ECP5 dedicated `USRMCLK` network, so there is no `flash_clk` GPIO assignment in LPF.

### Pin Protection

- Add 100 nF (0.1 µF) decoupling capacitor near VCC (pin 8)
- Pull-ups not required (SPI flash handles I/O directly)
- No external protection needed (3.3 V logic levels)

## Wishbone Interface

**Module**: `wb_flash.sv`  
**Address**: `0x10007000` (proposed)  
**Registers**:

| Offset | Name | R/W | Bits | Description |
|--------|------|-----|------|-------------|
| 0x00 | STATUS | R | [1:0] | `[0]`=busy, `[1]`=reserved |
| 0x04 | CONTROL | W | [2:0] | `[0]`=read, `[1]`=write, `[2]`=erase |
| 0x08 | ADDRESS | W | [23:0] | Flash address (24-bit) |
| 0x0C | DATA | R/W | [7:0] | Page buffer (256 bytes accessed as bytes) |

### Operation Sequence

#### Read a page (256 bytes)

```c
volatile uint32_t* addr = (volatile uint32_t*)0x10007008;
volatile uint32_t* ctrl = (volatile uint32_t*)0x10007004;
volatile uint32_t* status = (volatile uint32_t*)0x10007000;
volatile uint32_t* data = (volatile uint32_t*)0x1000700C;

// Set address
*addr = 0x1000;  // Read from offset 0x1000

// Trigger read
*ctrl = (1 << 0);  // CTRL[0] = 1 → read

// Wait for completion
while (*status & 1) {}  // Poll until busy flag clears

// Read 256 bytes
uint8_t page[256];
for (int i = 0; i < 256; i++) {
    page[i] = (uint8_t)(*data);
}
```

#### Write a page (256 bytes)

```c
// Set address (must be page-aligned)
*addr = 0x2000;

// Fill buffer
for (int i = 0; i < 256; i++) {
    *data = my_data[i];
}

// Trigger write
*ctrl = (1 << 1);  // CTRL[1] = 1 → write

// Wait for completion
while (*status & 1) {}
```

#### Erase a sector (4 KB)

```c
// Set address (any byte in the 4 KB sector)
*addr = 0x4000;

// Trigger erase
*ctrl = (1 << 2);  // CTRL[2] = 1 → erase

// Wait for completion (~100 ms typical)
while (*status & 1) {}
```

## Firmware API

### Overview

The low-level API is provided by the `Flash` class, and KV storage is provided by `flashdb_port` wrappers.

### Safety Features

**Boot region is PROTECTED at the firmware level:**
- Any write attempt to 0x000000–0x1FFFFF (2 MB) returns `false`
- Any erase attempt to 0x000000–0x1FFFFF returns `false`
- Functions silently reject invalid operations (safe for embedded systems)

### Low-Level API (`Flash` class)

```cpp
#include "flash.h"

Flash flash(0x10007000u);

bool ok = flash.lowLevelTest();
ok = flash.readPage(0x200000u, page_buf_256);
ok = flash.writePage(0x200000u, page_buf_256);
ok = flash.eraseSector(0x200000u);
```

### FlashDB KV API (`flashdb_port.h`)

```cpp
// Init once at boot
flashdb_init(&flash, status_callback);

// Optional smoke test
flashdb_boot_counter_test(status_callback);

// Typed helpers
flashdb_set_i32("channel", 6);
int32_t channel = 0;
flashdb_get_i32("channel", &channel);

flashdb_set_str("hostname", "colorlight-i9");
char host[32] = {};
flashdb_get_str("hostname", host, sizeof(host));
```

### Memory Layout

```
Flash Address Space (8 MB total)
├─ 0x000000–0x1FFFFF (2 MB)      ← FPGA Boot Configuration (PROTECTED!)
├─ 0x200000–0x203FFF (16 KB)     ← Parameter/scratch region (safe for PicoRV32)
│  ├─ Sector 128 (0x200000–0x200FFF)
│  ├─ Sector 129 (0x201000–0x201FFF)
│  ├─ Sector 130 (0x202000–0x202FFF)
│  └─ Sector 131 (0x203000–0x203FFF)
├─ 0x204000–0x243FFF (256 KB)    ← FlashDB KV partition (`nodenet_kv`)
└─ 0x244000–0x7FFFFF (~5.73 MB)  ← Application data
```

## Integration Checklist

- [x] Pin assignments present in `colorlight_i9.lpf` for MOSI, MISO, CS_N (CLK via USRMCLK)
- [ ] Add decoupling capacitor (100 nF) near flash VCC
- [x] `wb_flash` instantiated in `top.sv` with Wishbone mux
- [ ] Verify pin voltage levels (3.3 V)
- [x] Test with `flashdb_init()` and typed KV get/set wrappers

## Testing

### Test 1: Read Flash ID

```cpp
// Sends JEDID command (0x9F) and reads 3 manufacturer ID bytes
// Expected: 0xEF 0x40 0x17 for W25Q64
```

### Test 2: Write and Read Back

```cpp
uint8_t test_buf[256];
for (int i = 0; i < 256; i++) test_buf[i] = i & 0xFF;

flash_erase_sector(4);  // Erase sector 4 (0x4000)
flash_write_page(0x4000, test_buf);
flash_read_page(0x4000, test_buf);

// Verify all bytes match
bool ok = true;
for (int i = 0; i < 256; i++) {
    if (test_buf[i] != (i & 0xFF)) ok = false;
}
```

### Test 3: FlashDB boot counter

```cpp
bool ok = flashdb_init(&flash, status_callback);
if (ok) {
    flashdb_boot_counter_test(status_callback);
    // Expect status callback line: "[FDB] boot cnt updated"
}
```

## Known Limitations

1. `Flash::writePage()` expects page-sized (256 B) writes at page-aligned base addresses.
2. `Flash::eraseSector()` expects sector-aligned addresses (4 KB).
3. Flash programming cannot flip bits from 0 back to 1 without erase (NOR flash rule).
4. The 2 MB boot region is intentionally write-protected by firmware safety checks.

## Future Enhancements

- [ ] Wear-leveling (round-robin sector allocation)
- [ ] CRC-32 on parameter blocks
- [ ] Automatic parameter region recycling
- [ ] Support for larger values (multi-page)
- [ ] Performance optimizations (pipelined SPI transfers)

## References

- **W25Q64 Datasheet**: https://www.winbond.com/hq/product/code-storage-flash-memory/ (search for W25Q64)
- **SPI Flash Protocol**: Standard JEDEC SPI flash command set
- **FlashDB**: https://github.com/armink/FlashDB

