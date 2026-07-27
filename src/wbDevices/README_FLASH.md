# SPI Flash (W25Q64) Module — Parameter Storage

## Overview

The W25Q64JVSIQ is an 8 MB SPI flash memory IC on the Colorlight i9 that provides persistent storage for:
- **Application parameters** (like Arduino preferences)
- **Calibration data**, settings, logs
- **Firmware backups** or configuration images

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
| CLK | (to assign) | 6 (CLK) | SPI clock |
| MOSI | (to assign) | 5 (DI) | Master out, slave in |
| MISO | (to assign) | 2 (DO) | Master in, slave out |
| CS_N | (to assign) | 1 (CS) | Chip select (active low) |
| VCC | 3.3 V | 8 (VCC) | Power supply |
| GND | GND | 4 (GND) | Ground |

> **Note**: Pin assignments must be added to `constraints/colorlight_i9.lpf`

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

The `flash.h` header provides:
1. **Low-level page/sector operations** for direct flash access
2. **Parameter storage API** (key-value store, like Arduino preferences)
3. **Convenience functions** for common types (int32, strings)
4. **Automatic protection** against boot region overwrites

### Safety Features

**Boot region is PROTECTED at the firmware level:**
- Any write attempt to 0x000000–0x1FFFFF (2 MB) returns `false`
- Any erase attempt to 0x000000–0x1FFFFF returns `false`
- Functions silently reject invalid operations (safe for embedded systems)

Example:
```cpp
flash_put("test", ...);  // Writes to 0x200000 (safe, after boot region)
flash_write_page(0x100, ...);  // Returns false (in boot region!)
flash_erase_sector(0);  // Returns false (in boot region!)
```

### Low-Level API

```cpp
#include "flash.h"

void flash_wait_ready();                          // Poll until ready
void flash_read_page(uint32_t offset, uint8_t* buf);   // Read 256 bytes (no protection check)
bool flash_write_page(uint32_t offset, const uint8_t* buf);  // Write 256 bytes (protected!)
bool flash_erase_sector(uint16_t sector);        // Erase 4 KB sector (protected!)
bool flash_is_safe_address(uint32_t offset);     // Check if address is outside boot region
```

### Parameter Storage (Key-Value API)

```cpp
// Store a parameter
flash_put("wifi_ssid", (uint8_t*)"MyNetwork", 9);
flash_put_string("hostname", "colorlight-i9");
flash_put_int("channel", 6);

// Retrieve a parameter
uint8_t buf[256];
uint16_t len = flash_get("wifi_ssid", buf, sizeof(buf));
if (len > 0) {
    // Parameter found (len bytes in buf)
}

// Retrieve with type conversion
char ssid[32];
flash_get_string("wifi_ssid", ssid, sizeof(ssid), "DefaultSSID");

int channel = flash_get_int("channel", 1);  // Returns 1 if not found
```

### Memory Layout

```
Flash Address Space (8 MB total)
├─ 0x000000–0x1FFFFF (2 MB)      ← FPGA Boot Configuration (PROTECTED!)
├─ 0x200000–0x203FFF (16 KB)     ← Parameter region (safe for PicoRV32)
│  ├─ Sector 128 (0x200000–0x200FFF)
│  ├─ Sector 129 (0x201000–0x201FFF)
│  ├─ Sector 130 (0x202000–0x202FFF)
│  └─ Sector 131 (0x203000–0x203FFF)
└─ 0x204000–0x7FFFFF (7.75 MB)   ← Application data
```

### Parameter Entry Format

Each parameter is stored as:
```
[key_len(1B)] [value_len_lo(1B)] [value_len_hi(1B)] [key(N)] [value(M)]
```

Example: Store "temp" = 25.5°C

```
Key:   "setpoint" (8 bytes)
Value: [0xCC, 0x00] (2 bytes) representing 0x00CC in little-endian

[0x08] [0x02] [0x00] ['s']['e']['t']['p']['o']['i']['n']['t'] [0xCC] [0x00]
```

### Wear-Leveling Strategy

The current implementation uses **simple append semantics**:
- New parameters written sequentially to end of region
- Old values NOT automatically erased (wastes space)
- Recommended: Periodically compact region by reading all params, erasing sector, re-writing

Example compaction:

```cpp
void flash_compact() {
    // 1. Read all parameters from current region
    // 2. Erase all 4 sectors (0–3)
    // 3. Write parameters back sequentially
    // 4. Done!
}
```

## Integration Checklist

- [ ] Add pin assignments to `colorlight_i9.lpf` (CLK, MOSI, MISO, CS_N)
- [ ] Add decoupling capacitor (100 nF) near flash VCC
- [ ] Instantiate `wb_flash` in top.sv with Wishbone mux
- [ ] Verify pin voltage levels (3.3 V)
- [ ] Test with `flash_get_string()` / `flash_put_string()` in firmware

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

### Test 3: Parameter Storage

```cpp
flash_put_string("test_key", "Hello Flash!");
char buf[32];
flash_get_string("test_key", buf, sizeof(buf), "");
// buf should contain "Hello Flash!"
```

## Known Limitations

1. **No automatic wear-leveling**: Parameters written sequentially, old values not recycled
2. **Parameter region limited**: 16 KB for all parameters combined
3. **No CRC/checksum**: Bit errors not detected (optional enhancement)
4. **Page-aligned writes**: Cannot write partial pages
5. **No read-modify-write**: Updating a 1-byte field requires read whole page + rewrite

## Future Enhancements

- [ ] Wear-leveling (round-robin sector allocation)
- [ ] CRC-32 on parameter blocks
- [ ] Automatic parameter region recycling
- [ ] Support for larger values (multi-page)
- [ ] Performance optimizations (pipelined SPI transfers)

## References

- **W25Q64 Datasheet**: https://www.winbond.com/hq/product/code-storage-flash-memory/ (search for W25Q64)
- **SPI Flash Protocol**: Standard JEDEC SPI flash command set
- **Arduino Preferences Library**: Inspiration for simple key-value API

