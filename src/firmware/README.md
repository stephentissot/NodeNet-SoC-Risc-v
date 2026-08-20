# Firmware — RISC-V C++17 Bare-Metal

Firmware for the Colorlight i9 RISC-V SoC. Written in C++17, compiled with `riscv-none-elf-g++`, no operating system, no Arduino.

## Build System

```bash
# From the project root
make firmware-build       # stage0 bootloader (same as firmware-bootloader)
make firmware-bootloader  # explicit stage0 bootloader target
make firmware-image       # package SDRAM payload with stage0 header
make flash-fw             # program application image at 0x244000 in SPI flash
make all                  # firmware + FPGA bitstream
make clean                # remove all build artifacts

# From src/firmware
make firmware-app-build   # SDRAM application ELF only
```

`make firmware-build` also prints a compact size report:
- ELF text/data/bss bytes
- RAM usage (`.data + .bss`) and percentage
- HEX file size and payload bytes
- ROM usage (`payload / ROM_CAPACITY_BYTES`) and percentage

The build fails if HEX payload exceeds configured ROM capacity.

After cloning, fetch the u8g2 submodule first:
```bash
git submodule update --init
```

### Toolchain flags
| Flag | Purpose |
|------|---------|
| `-std=c++17` | C++17 language standard |
| `-Os` | Optimize for size |
| `-ffreestanding` | No standard library assumptions |
| `-ffunction-sections -fdata-sections` | Enable per-function dead code elimination |
| `-Wl,--gc-sections` | Linker removes unused functions/data (critical for u8g2) |
| `-nostartfiles` | Don't use toolchain crt0 (we have `start.S`) |
| `--specs=nano.specs` | newlib-nano: provides `memcpy`, `strlen`, `memset`, etc. |
| `--specs=nosys.specs` | Stub syscalls (`_write`, `_sbrk`, …) for bare-metal |
| `-fno-exceptions -fno-rtti` | No C++ exception/RTTI overhead |

### File Structure

```
src/firmware/
├── start.S              SDRAM application startup: gp/sp init + .data/.bss + .init_array + call main
├── link.ld              Legacy ROM-linked firmware linker script
├── link_app_sdram.ld    Runtime application linker script for execution from SDRAM
├── main.cpp             Application entry point executed after stage0 handoff
├── i2c.h            I2C MMIO driver (wb_i2c peripheral)
├── sdram.h              SDRAM helpers (`SDRAM_DATA`, readiness wait, scratch-area self-tests)
├── sdram.cpp            Single-TU SDRAM probe and scratch storage for self-tests
└── lib/
    ├── modbus/      Modbus RTU master MMIO driver for wb_modbus_master
    │   ├── ModbusMaster.h
    │   └── ModbusMaster.cpp
    ├── serial/      UART1 MMIO helper (Arduino-style API)
    │   ├── Serial.h
    │   └── Serial.cpp
    ├── u8g2/        u8g2 graphics library (git submodule)
    │   └── csrc/    132 C source files compiled with --gc-sections
    └── u8g2_hal/    u8g2 hardware abstraction layer (our code)
        ├── u8g2_hal.h   HAL declarations
        └── u8g2_hal.cpp I2C callback + delay callback
```

Related bootloader sources live in `src/bootloader/`:

```text
src/bootloader/
├── start.S          Stage0 ROM startup
├── link.ld          Stage0 ROM linker script
└── boot_stage0.cpp  SPI flash image validation, SDRAM copy, CRC, jump to app
```

## Runtime Boot Architecture

1. `boot_stage0` executes from ROM at `0x00000000`.
2. Stage0 reads the firmware image header from SPI flash offset `0x244000`.
3. After header and payload CRC validation, stage0 copies the application into SDRAM at `0x20000000` and jumps to the entry point.
4. `start.S` runs from SDRAM, initializes `.data`/`.bss`, calls global constructors, then enters `main()`.
5. Runtime SDRAM self-tests use a dedicated scratch area and must not overwrite the image currently executing from SDRAM.

---

## Memory Map

| Address Range | Size | Description |
|--------------|------|-------------|
| `0x00000000–0x0000FFFF` | 64 KiB | **Boot ROM** — stage0 bootloader only |
| `0x00010000–0x0001FFFF` | 64 KiB | **RAM** — stack, BSS, initialized data |
| `0x10000000` | 4 B | **D2 LED GPIO** (`wb_gpio`) |
| `0x10000004` | 4 B | **RJ45 LED0** (`wb_led`) |
| `0x10000008` | 4 B | **RJ45 LED1** (`wb_led`) |
| `0x10004000` | 32 B | **UART1 Modbus master** (`wb_modbus_master`) |
| `0x10005000` | 32 B | **I2C0** |
| `0x10006000` | 32 B | **NodeNet485** (RS-485 @ 1 Mb/s, includes LED pulse config) |
| `0x10007000` | 32 B | **SPI Flash** (`wb_flash`, W25Q64 via USRMCLK) |
| `0x20000000–0x207FFFFF` | 8 MB | **SDRAM** — runtime app image, `SDRAM_DATA`, large buffers |

---

## Peripheral Examples

Flash persistence status snapshot (current `main.cpp` boot flow):
- `[FLASH] LowLevel PASS`
- `[FDB] Ready`
- `[FDB] boot cnt updated`
- The flash driver also exposes a stable ASCII `deviceId` derived from the W25Q64 factory UID and shown on the OLED at boot.

### D2 LED GPIO (`0x10000000`)

```cpp
#define LED (*(volatile uint32_t*)0x10000000)

LED = 1;    // ON
LED = 0;    // OFF
LED ^= 1;   // Toggle
```

Current `main.cpp` heartbeat policy uses a non-blocking software toggle every 500 ms.

### RJ45 LEDs (`wb_led`: `0x10000004`, `0x10000008`)

RJ45 LEDs are controlled through the `wb_led` peripheral and firmware helpers in `include/led.h`.
Hardware is wired active-low with pull-up, but `Led::On/Off/Blink` remain logical (On = visible LED ON).

```cpp
#include "led.h"

#define LED0_BASE 0x10000004UL
#define LED1_BASE 0x10000008UL

wb_led::Led led0(LED0_BASE, false);
wb_led::Led led1(LED1_BASE, false);

led0.Blink(100);   // non-blocking one-shot pulse (100 ms)
led1.On();         // set default state ON
led1.Off();        // set default state OFF
```

These two addresses remain available for diagnostics or visual activity signaling.

---

### UART1 (`wb_modbus_master`: `0x10004000`) — Modbus RTU Master

The firmware includes a Modbus RTU master helper in `lib/modbus/ModbusMaster.h` and `lib/modbus/ModbusMaster.cpp`.

`wb_modbus_master` is a Wishbone wrapper around `uart_simple` with hardware-managed:
- RTU transaction timing
- CRC16 TX generation and RX validation
- timeout / retry / status flags

Minimal usage example:

```cpp
#include "ModbusMaster.h"

constexpr uint32_t MODBUS1_BASE = 0x10004000u;
constexpr uint8_t MODBUS_SLAVE = 0x01u;
ModbusMaster modbus1(MODBUS1_BASE);

int main() {
    modbus1.begin(9600, 500, 2);   // baud, timeout_ms, retries
    modbus1.setInterframeCharsQ1(14);

    uint16_t version = 0;
    if (modbus1.readHoldingRegisters(MODBUS_SLAVE, 0x8000, 1, &version)) {
        // Waveshare encoding: 0x0064 => V1.00
    }

    for (;;) {
        (void)modbus1.writeSingleCoil(MODBUS_SLAVE, 0x0000, true);
    }
}
```

Useful methods:
- `begin(baud, timeout_ms, retries)`
- `setInterframeCharsQ1(chars_q1)`
- `readCoils`, `readDiscreteInputs`, `readHoldingRegisters`, `readInputRegisters`
- `writeSingleCoil`, `writeMultipleCoils`, `writeSingleRegister`, `writeMultipleRegisters`
- `lastError()`, `lastExceptionCode()`, `lastHwStatus()`

Legacy note: `lib/serial/Serial` remains available as a standalone utility class but UART1 hardware is now mapped to Modbus RTU master in the default top-level SoC.

---

### NodeNet485 (`0x10006000`) — Multi-Node RS-485 Mailbox Transport

The NodeNet485 protocol enables reliable multi-node communication over RS-485 at 1 Mb/s.

Validation snapshot (2026-08-11):
- Automatic heartbeat from HDL validated on hardware at ~10 s period.
- RX path validated for frames addressed to the local node.
- TX path validated end-to-end.
- Runtime baud validated at 115200 and 1 Mb/s.
- Pending targeted checks: broadcast acceptance and non-matching destination ignore behavior.

**Mailbox Register Model**:
- **TX staging**: firmware writes one message into TX command/data registers
- **RX mailbox**: hardware exposes one decoded message header and a byte stream reader
- **Frame encoding/decoding** happens inside `wb_nodenet.sv`
- **Capacity**: one staged TX message and one decoded RX message at a time

**Register Flow** (Firmware Controls):

```
TX Path (Firmware → Hardware):
    1. Write TX_CMD = [dst | len]
    2. Write payload bytes through TX_DATA
    3. Write CONTROL.bit0 to trigger transmission
    4. Hardware schedules, frames, and transmits over UART

RX Path (Hardware → Firmware):
    1. Hardware receives, decodes, and validates a frame
    2. Hardware exposes source + length through RX_HDR
    3. Firmware checks RX valid bit
    4. Firmware drains payload bytes through RX_DATA
```

**Complete API** (from `include/nodenet.h`):

```cpp
#include "nodenet.h"

// Preferred object API
constexpr uint32_t NODENET0_BASE = 0x10006000u;
NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);

// Send unicast message to node 0x02
myNodeNet.Send(0x02, "Hello", 5);

// Send broadcast to all nodes
myNodeNet.Broadcast("ALERT");

// Receive messages (check first to avoid blocking)
if (myNodeNet.HasMessage()) {
    NodeNetMessage msg = myNodeNet.ReadMessage();
    printf("From node 0x%02X: len=%d\n", msg.src_addr, msg.len);
    
    // Echo back if not broadcast
    if (msg.src_addr != 0) {
        myNodeNet.Send(msg.src_addr, msg.data, msg.len);
    }
    
    NodeNet::FreeMessage(msg);  // IMPORTANT: deallocate!
}
```

**Detailed API**:

```cpp
class NodeNet {
public:
    explicit NodeNet(uint32_t base, uint8_t addr, NodeNetPriority priority, uint32_t led_blink_ms = 100u);
    void Init(uint8_t addr, NodeNetPriority priority, uint32_t led_blink_ms = 100u);
    uint32_t Status() const;
    bool TxMailboxReady() const;
    bool TxHasSpace(uint16_t msg_len) const;
    bool HasMessage() const;
    uint8_t MessageCount() const;
    void Send(uint8_t dst, const uint8_t* data, uint16_t len) const;
    void Send(uint8_t dst, const char* str) const;
    void Broadcast(const uint8_t* data, uint16_t len) const;
    void Broadcast(const char* str) const;
    NodeNetMessage ReadMessage() const;
    static void FreeMessage(NodeNetMessage& msg);
};
```

**Echo Loop Example** (Listen and reply):

```cpp
int main() {
    constexpr uint32_t NODENET0_BASE = 0x10006000u;
    NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
    
    while (1) {
        if (myNodeNet.HasMessage()) {
            NodeNetMessage msg = myNodeNet.ReadMessage();
            
            // Echo back unicast messages to sender
            if (msg.src_addr != 0) {
                myNodeNet.Send(msg.src_addr, msg.data, msg.len);
            }
            
            NodeNet::FreeMessage(msg);  // Don't forget!
        }
    }
}
```

**Multi-Node Query Example** (Send and wait for response):

```cpp
int main() {
    constexpr uint32_t NODENET0_BASE = 0x10006000u;
    NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
    
    // Send request to node 0x02
    myNodeNet.Send(0x02, "STATUS?", 7);
    
    // Wait for response (timeout = 1 second)
    uint32_t deadline = get_cycles() + 25_000_000;
    
    while (get_cycles() < deadline) {
        if (myNodeNet.HasMessage()) {
            NodeNetMessage msg = myNodeNet.ReadMessage();
            
            if (msg.src_addr == 0x02) {
                // Got response!
                printf("Temperature: %.*s\n", msg.len, msg.data);
                NodeNet::FreeMessage(msg);
                return 0;
            }
            
            NodeNet::FreeMessage(msg);
        }
    }
    
    printf("No response from node 0x02 (timeout)\n");
    return 1;
}
```

**Mailbox Debugging**:

```cpp
NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
uint32_t status = myNodeNet.Status();
bool tx_busy = (status & (NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE)) != 0;
bool rx_ready = (status & NODENET_STATUS_RX_VALID) != 0;
bool rx_error = (status & (NODENET_STATUS_RX_ERROR | NODENET_STATUS_RX_OVERFLOW)) != 0;

uint32_t header = *(volatile uint32_t *)(NODENET0_BASE + NODENET_RX_HDR_OFS);
uint8_t src = header >> 24;
uint16_t len = header & 0xFFFF;
```

**Performance Characteristics**:
- **Baud Rate**: 1 Mb/s (25-cycle divisor @ 25 MHz)
- **Throughput**: ~40–50 kB/s effective (after protocol overhead)
- **Message Latency**: ~10 ms for 64-byte message
- **Mailbox Capacity**: one staged TX message + one decoded RX message
- **Anti-Collision**: Automatic backoff per node address
  - Broadcast: 50 ms × node_addr
  - Unicast: 2 ms × node_addr
- **Heartbeat**: Default ~10 seconds (configurable)

**Hardware Integration**:
- **RX Pin**: G5 (input from RS-485 transceiver)
- **TX Pin**: D16 (output to RS-485 transceiver)
- **Driver Enable**: Automatic (hardware module handles)
- **Current Status**: Functional TX/RX framing with mailbox-based Wishbone API

For full protocol documentation including frame format, CRC, and encoding details, see [../wbDevices/README_NODENET.md](../wbDevices/README_NODENET.md).

---

### SPI Flash (`0x10007000`) — Low-Level Access + FlashDB KV Storage

The onboard W25Q64 (8 MB) is exposed through the `wb_flash` peripheral.
`flash.h` provides low-level page/sector operations, a factory UID reader, and a stable ASCII `deviceId` helper; `flashdb_port.h` provides typed key-value helpers.

```cpp
#include "flash.h"
#include "flashdb_port.h"

Flash flash(0x10007000u);
flashdb_init(&flash, nullptr);

flashdb_set_str("device_name", "nodenet-01");
char name[32] = {};
flashdb_get_str("device_name", name, sizeof(name));

char device_id[12] = {};
flash.readUniqueIdAscii(device_id, sizeof(device_id));
```

Implementation notes:
- SPI SCK uses the ECP5 dedicated USRMCLK path (not a normal GPIO pin).
- Firmware-side protection keeps writes/erases out of the boot region.
- FlashDB uses partition `nodenet_kv` at `0x204000–0x243FFF`.
- The ASCII `deviceId` is derived from the 64-bit factory UID and contains only `a-z`, `A-Z`, and `0-9`.

Detailed register-level documentation is available in [../wbDevices/README_FLASH.md](../wbDevices/README_FLASH.md).

---

### SDRAM (`0x20000000`, 8 MB available for application)

⚠️ **SDRAM Allocation**:
- **0x20000000–0x207FFFFF (8 MB)** ← Available for PicoRV32 application and large buffers

**Important**: the SDRAM controller performs a ~200 µs initialization at power-on. Do not access SDRAM before `sdram_wait_ready()` returns.

**Current validation status**:
- `sdramTest()` runs at boot and checks several reserved scratch regions plus a byte-pattern test.
- A dedicated `SDRAM_DATA` probe object is linked from a single translation unit and verified at runtime.
- Full firmware execution from SDRAM is validated.
- Sub-word CPU stores now work through the Wishbone SDRAM wrapper via read-modify-write.

```cpp
#include "sdram.h"

// Place large variables in SDRAM at link time
// (linker assigns addresses automatically from SDRAM_BASE)
SDRAM_DATA uint8_t  frame_buffer[128 * 64 / 8];  // 1 KB framebuffer
SDRAM_DATA uint32_t modbus_log[4096];             // 16 KB log

int main() {
    // Must be called before any SDRAM access
    sdram_wait_ready();

    // Zero-initialize (SDRAM content is undefined at power-on)
    __builtin_memset(frame_buffer, 0, sizeof(frame_buffer));

    // Use linker-placed SDRAM objects directly
    modbus_log[0] = 0xDEADBEEF;

    // Memory test (reserved scratch area, safe while code executes from SDRAM)
    uint32_t errors = sdram_test(1024);  // Test 4 KB inside scratch space
    if (errors == 0) uart_puts("SDRAM OK\n");
}
```

Avoid destructive writes to `SDRAM_BASE` unless you control the placement and know you are outside the currently executing app image.

For the integrated boot self-test path used by `main.cpp`:

```cpp
if (sdramTest(oled_boot_status)) {
    oled_write("[BOOT] System ready");
} else {
    oled_write("[BOOT] Degraded mode");
}
```

**Allocating large buffers dynamically** (pointer arithmetic):
```cpp
// Manual allocator from SDRAM base
// Coordinate this with the linker map so allocations do not overlap the app image.
static uintptr_t sdram_ptr = SDRAM_BASE;

void* sdram_alloc(size_t bytes) {
    void* p = (void*)sdram_ptr;
    sdram_ptr += (bytes + 3) & ~3;  // align to 4 bytes
    
    // Safety check: don't allocate beyond 8 MB boundary
    if (sdram_ptr > 0x20800000) return nullptr;
    return p;
}
```

**ArduinoJson 7 with the SDRAM allocator**:

The firmware integrates ArduinoJson 7 in freestanding mode. For small documents, a regular `JsonDocument` is fine. For larger payloads, you can move the JSON heap into SDRAM with `g_sdram_json_allocator`.

Rules:
- Call `sdram_wait_ready()` before any SDRAM access.
- Call `sdram_json_allocator_init()` once before the first `JsonDocument doc(&g_sdram_json_allocator);`.
- The default JSON pool is `SDRAM_JSON_POOL_SIZE` (`256 KiB`) in `g_sdram_json_pool`.
- The pool lives in SDRAM, so its content is undefined after reset until you initialize it.

```cpp
#include <ArduinoJson.h>
#include "sdram.h"

static void json_example(void)
{
    if (!sdram_wait_ready()) {
        uart_puts("SDRAM not ready\n");
        return;
    }

    if (!sdram_json_allocator_init()) {
        uart_puts("JSON allocator init failed\n");
        return;
    }

    JsonDocument doc(&g_sdram_json_allocator);
    doc["node"] = "NodeNet";
    doc["uptime_ms"] = *TIMER_MS;
    doc["sdram"] = true;

    char buffer[128];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len < sizeof(buffer)) {
        buffer[len] = '\0';
        uart_puts(buffer);
        uart_puts("\n");
    }
}
```

This allocator is implemented in `sdram.cpp` as a simple free-list over `g_sdram_json_pool`. It supports `allocate()`, `deallocate()`, and `reallocate()`, which matches what ArduinoJson 7 expects from a custom allocator.

**SDRAM Regions (Memory Map)**:
```
Hardware: 8 MB total (M12L64322A SDRAM on Colorlight i9)
└─ 0x20000000 ─ 0x207FFFFF (8 MB)  ← Runtime image + application SDRAM region
    ├─ `.text` / `.rodata` / `.data` / `.bss` for the SDRAM-linked app
    ├─ `SDRAM_DATA` variables (placed by linker)
    ├─ Reserved scratch space for `sdramTest()`
    └─ Remaining free space / manual allocation if coordinated with the linker map
```

**Constants in `sdram.h`**:
```cpp
#define SDRAM_BASE  0x20000000UL
#define SDRAM_SIZE  (8UL * 1024 * 1024)
```

---

### I2C0 (`0x10005000`) — via `i2c.h`

`i2c.h` provides an `I2c` API for the `wb_i2c` wrapper around
`i2c_master_wbs_16`.

```cpp
#include "i2c.h"

void i2c_example(void) {
    I2c i2c;

    // Set clock: 400 kHz @ 25 MHz (prescale = 25e6 / (400e3 * 4) = 15)
    i2c.begin(15);

    // Write: send a 2-byte command to device at address 0x3C
    uint8_t cmd[] = { 0x00, 0xAF };     // SSD1306: Co=0, D/C=0, DISPLAY_ON
    uint8_t rc = i2c.write(0x3C, cmd, 2);  // I2c::I2C_OK on success
    if (rc != I2c::I2C_OK) uart_puts("I2C write failed\n");
}
```

**Raw MMIO access** (for custom protocols, 16-bit register words in 32-bit stride):
```cpp
constexpr uint32_t I2C0_BASE = 0x10005000u;
auto i2c_reg = [](uint32_t base, uint32_t ofs) -> volatile uint32_t& {
    return *reinterpret_cast<volatile uint32_t*>(base + ofs);
};

constexpr uint32_t I2C16_REG_STATUS = 0x00u;
constexpr uint32_t I2C16_REG_CMD    = 0x04u;
constexpr uint32_t I2C16_REG_DATA   = 0x08u;
constexpr uint32_t I2C16_REG_PRESC  = 0x0Cu;

constexpr uint16_t I2C16_CMD_START      = (1u << 8);
constexpr uint16_t I2C16_CMD_WRITE_MULT = (1u << 11);
constexpr uint16_t I2C16_CMD_STOP       = (1u << 12);
constexpr uint16_t I2C16_DATA_VALID     = (1u << 8);
constexpr uint16_t I2C16_DATA_LAST      = (1u << 9);
constexpr uint16_t I2C16_FIFO_WR_FULL   = (1u << 12); // STATUS bit
constexpr uint16_t I2C16_FIFO_CMD_FULL  = (1u << 9);  // STATUS bit

// Set prescaler for 100 kHz @ 25 MHz
i2c_reg(I2C0_BASE, I2C16_REG_PRESC) = 62u;

// Push one data byte with DATA_VALID and DATA_LAST
while (i2c_reg(I2C0_BASE, I2C16_REG_STATUS) & I2C16_FIFO_WR_FULL);
i2c_reg(I2C0_BASE, I2C16_REG_DATA) = (0xAFu | I2C16_DATA_VALID | I2C16_DATA_LAST);

// Push START + WRITE_MULT + STOP to slave 0x3C
while (i2c_reg(I2C0_BASE, I2C16_REG_STATUS) & I2C16_FIFO_CMD_FULL);
i2c_reg(I2C0_BASE, I2C16_REG_CMD) = (0x3Cu | I2C16_CMD_START | I2C16_CMD_WRITE_MULT | I2C16_CMD_STOP);

// Poll status busy bit until transfer complete
while (i2c_reg(I2C0_BASE, I2C16_REG_STATUS) & 0x1u) {}
```

---

### OLED Display — u8g2 + `u8g2_hal.h`

u8g2 is cloned as a git submodule in `lib/u8g2/`.  
The HAL (`u8g2_hal.h`/`u8g2_hal.cpp`) connects u8g2 to the hardware I2C peripheral.

**Wiring**: SSD1306 OLED → SCL=D18, SDA=D17 with 4.7 kΩ pullups to 3.3 V.

#### 128×64 SSD1306

```cpp
#include "u8g2_hal.h"

u8g2_t display;

void oled_init(void) {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &display,
        U8G2_R0,              // No rotation
        u8x8_byte_i2c_hw,     // Our I2C callback (u8g2_hal.cpp)
        u8x8_gpio_delay_hw    // Our delay callback
    );
    u8g2_InitDisplay(&display);
    u8g2_SetPowerSave(&display, 0);  // Wake up display
}

void oled_hello(void) {
    u8g2_ClearBuffer(&display);
    u8g2_SetFont(&display, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&display, 0, 12, "RISC-V SoC");
    u8g2_SetFont(&display, u8g2_font_6x10_tr);
    u8g2_DrawStr(&display, 0, 26, "Colorlight i9");
    u8g2_DrawStr(&display, 0, 40, "25 MHz PicoRV32");
    u8g2_SendBuffer(&display);     // Push framebuffer to OLED
}

void oled_counter(uint32_t n) {
    char buf[16];
    // Simple integer to string (no sprintf to save ROM)
    uint8_t i = 0;
    do { buf[i++] = '0' + (n % 10); n /= 10; } while (n && i < 14);
    buf[i] = '\0';
    // Reverse
    for (uint8_t a=0, b=i-1; a<b; a++, b--) {
        char t=buf[a]; buf[a]=buf[b]; buf[b]=t;
    }
    u8g2_ClearBuffer(&display);
    u8g2_SetFont(&display, u8g2_font_ncenB14_tr);
    u8g2_DrawStr(&display, 0, 20, buf);
    u8g2_SendBuffer(&display);
}
```

#### 128×32 SSD1306

```cpp
// Change the setup function, everything else is identical
u8g2_Setup_ssd1306_i2c_128x32_univision_f(
    &display, U8G2_R0,
    u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
```

#### Available fonts (small selection)

| Font | Height | Width | Good for |
|------|--------|-------|----------|
| `u8g2_font_ncenB08_tr` | 8px | var | Small text |
| `u8g2_font_ncenB14_tr` | 14px | var | Medium titles |
| `u8g2_font_6x10_tr` | 10px | 6px | Fixed-width text |
| `u8g2_font_7x13_tr` | 13px | 7px | Readable labels |

Full font list: https://github.com/olikraus/u8g2/wiki/fntlistall

#### Complete main() example

```cpp
#include <stdint.h>
#include "sdram.h"
#include "i2c.h"
#include "u8g2_hal.h"

extern "C" void __cxa_pure_virtual() { while (1); }

#define LED        (*(volatile uint32_t*)0x10000000)
#define UART0_DATA (*(volatile uint32_t*)0x10001000)
#define UART0_BAUD (*(volatile uint32_t*)0x10001008)

static u8g2_t display;

int main() {
    // 1. Init UART
    UART0_BAUD = 27;

    // 2. Init SDRAM (wait ~200 µs for controller init)
    sdram_wait_ready();

    // 3. Init OLED
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &display, U8G2_R0,
        u8x8_byte_i2c_hw, u8x8_gpio_delay_hw);
    u8g2_InitDisplay(&display);
    u8g2_SetPowerSave(&display, 0);

    // 4. Display boot message
    u8g2_ClearBuffer(&display);
    u8g2_SetFont(&display, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&display, 0, 12, "SoC boot OK");
    u8g2_SendBuffer(&display);

    // 5. Blink + echo loop
    uint32_t tick = 0;
    for (;;) {
        LED = (tick++ >> 18) & 1;  // Blink ~3 Hz
    }
}
```

---

## C++ Startup Details

`start.S` runs before `main()`:

```asm
_start:
    la sp, _stack_top           # Set stack pointer (0x00020000)

    # Call C++ global constructors (.init_array)
    la   a0, __init_array_start
    la   a1, __init_array_end
.Lctor_loop:
    beq  a0, a1, .Lctor_done
    lw   t0, 0(a0)
    jalr t0
    addi a0, a0, 4
    j    .Lctor_loop
.Lctor_done:

    call main                   # Jump to application
```

**Implication**: global C++ objects with constructors (e.g. `u8g2_t display;`) are initialized automatically before `main()`.

## SDRAM Data Placement

Variables marked `SDRAM_DATA` are placed in the `.sdram` linker section:

```cpp
// In any .cpp file:
#include "sdram.h"

SDRAM_DATA uint8_t  big_buffer[512 * 1024];   // 512 KB in SDRAM
SDRAM_DATA uint32_t history[16384];            // 64 KB log in SDRAM

// ⚠️ These are NOT zero-initialized at boot!
// Zero-init explicitly if needed:
//   __builtin_memset(big_buffer, 0, sizeof(big_buffer));
```

The linker places these starting at `0x20000000`. Addresses are assigned in source-file link order.

**Call `sdram_wait_ready()` before the first access**, otherwise the CPU will stall until the SDRAM controller finishes its ~200 µs initialization sequence (which it will, eventually — but your code is blocked until then).
