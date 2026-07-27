# Firmware — RISC-V C++17 Bare-Metal

Firmware for the Colorlight i9 RISC-V SoC. Written in C++17, compiled with `riscv-none-elf-g++`, no operating system, no Arduino.

## Build System

```bash
# From the project root
make firmware-build    # firmware only
make all               # firmware + FPGA bitstream
make clean             # remove all build artifacts
```

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
├── start.S          Startup: stack init + .init_array (C++ global ctors) + call main
├── link.ld          Linker script: ROM 0x0, RAM 0x10000, SDRAM 0x20000000
├── main.cpp         Application entry point
├── i2c.h            I2C MMIO driver (wb_i2c peripheral)
├── sdram.h          SDRAM helpers (SDRAM_DATA macro, sdram_wait_ready)
└── lib/
    ├── u8g2/        u8g2 graphics library (git submodule)
    │   └── csrc/    132 C source files compiled with --gc-sections
    └── u8g2_hal/    u8g2 hardware abstraction layer (our code)
        ├── u8g2_hal.h   HAL declarations
        └── u8g2_hal.cpp I2C callback + delay callback
```

---

## Memory Map

| Address Range | Size | Description |
|--------------|------|-------------|
| `0x00000000–0x0000FFFF` | 64 KiB | **Boot ROM** — firmware binary |
| `0x00010000–0x0002FFFF` | 64 KiB | **RAM** — stack, BSS, initialized data |
| `0x10000000` | 4 B | **LED GPIO** |
| `0x10005000` | 32 B | **I2C0** |
| `0x10006000` | 32 B | **NodeNet485** (RS-485 @ 1 Mb/s) |
| `0x20000000–0x207FFFFF` | 8 MB | **SDRAM** (external, available after ~200 µs) |

---

## Peripheral Examples

### LED GPIO (`0x10000000`)

```cpp
#define LED (*(volatile uint32_t*)0x10000000)

LED = 1;    // ON
LED = 0;    // OFF
LED ^= 1;   // Toggle
```

---

### NodeNet485 (`0x10006000`) — Multi-Node RS-485 Mailbox Transport

The NodeNet485 protocol enables reliable multi-node communication over RS-485 at 1 Mb/s.

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

// Initialize as node 0x01 with NORMAL priority
nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);

// Send unicast message to node 0x02
nodenet0_send(0x02, "Hello", 5);

// Send broadcast to all nodes
nodenet0_broadcast("ALERT");

// Receive messages (check first to avoid blocking)
if (nodenet0_has_message()) {
    NodeNetMessage msg = nodenet0_read();
    printf("From node 0x%02X: len=%d\n", msg.src_addr, msg.len);
    
    // Echo back if not broadcast
    if (msg.src_addr != 0) {
        nodenet0_send(msg.src_addr, msg.data, msg.len);
    }
    
    nodenet0_free_message(msg);  // IMPORTANT: deallocate!
}
```

**Detailed Functions**:

```cpp
// Initialize node address and priority
void nodenet0_init(uint8_t addr, NodeNetPriority priority);

// Send message (blocks if TX FIFO full)
void nodenet0_send(uint8_t dst, const uint8_t *data, uint16_t len);
void nodenet0_send(uint8_t dst, const char *str);  // C-string variant

// Broadcast to all nodes (destination = 0x00)
void nodenet0_broadcast(const uint8_t *data, uint16_t len);
void nodenet0_broadcast(const char *str);

// Poll for incoming messages
bool nodenet0_has_message();
uint8_t nodenet0_message_count();  // 0 or 1

// Read message (allocates buffer, must free after use!)
struct NodeNetMessage {
    uint8_t src_addr;              // Sender's address
    uint16_t len;                  // Payload length
    uint8_t *data;                 // Dynamically allocated
};
NodeNetMessage nodenet0_read();
void nodenet0_free_message(NodeNetMessage &msg);

// Priority levels (affects transmission order)
enum NodeNetPriority {
    NODENET_PRIORITY_LOW = 0,
    NODENET_PRIORITY_NORMAL = 1,
    NODENET_PRIORITY_HIGH = 2
};
```

**Echo Loop Example** (Listen and reply):

```cpp
int main() {
    nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
    
    while (1) {
        if (nodenet0_has_message()) {
            NodeNetMessage msg = nodenet0_read();
            
            // Echo back unicast messages to sender
            if (msg.src_addr != 0) {
                nodenet0_send(msg.src_addr, msg.data, msg.len);
            }
            
            nodenet0_free_message(msg);  // Don't forget!
        }
    }
}
```

**Multi-Node Query Example** (Send and wait for response):

```cpp
int main() {
    nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
    
    // Send request to node 0x02
    nodenet0_send(0x02, "STATUS?", 7);
    
    // Wait for response (timeout = 1 second)
    uint32_t deadline = get_cycles() + 25_000_000;
    
    while (get_cycles() < deadline) {
        if (nodenet0_has_message()) {
            NodeNetMessage msg = nodenet0_read();
            
            if (msg.src_addr == 0x02) {
                // Got response!
                printf("Temperature: %.*s\n", msg.len, msg.data);
                nodenet0_free_message(msg);
                return 0;
            }
            
            nodenet0_free_message(msg);
        }
    }
    
    printf("No response from node 0x02 (timeout)\n");
    return 1;
}
```

**Mailbox Debugging**:

```cpp
uint32_t status = nodenet_status();
bool tx_busy = (status & (NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE)) != 0;
bool rx_ready = (status & NODENET_STATUS_RX_VALID) != 0;
bool rx_error = (status & (NODENET_STATUS_RX_ERROR | NODENET_STATUS_RX_OVERFLOW)) != 0;

uint32_t header = *(volatile uint32_t *)NODENET_RX_HDR;
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
- **RX Pin**: H16 (input from RS-485 transceiver)
- **TX Pin**: H17 (output to RS-485 transceiver)
- **Driver Enable**: Automatic (hardware module handles)
- **Current Status**: Functional TX/RX framing with mailbox-based Wishbone API

For full protocol documentation including frame format, CRC, and encoding details, see [../src/wbDevices/README_NODENET.md](../src/wbDevices/README_NODENET.md).

---

### UART0 Removed

**Note**: UART0 has been replaced by NodeNet485 at address `0x10006000`. The hardware pins (H16/H17) are now used for RS-485 communication instead of direct UART. If you need serial debugging, consider using I2C + a USB bridge adapter, or add a separate debug UART on unused pins.

---

### SDRAM (`0x20000000`, 8 MB available for application)

⚠️ **SDRAM Allocation**:
- **0x20000000–0x207FFFFF (8 MB)** ← Available for PicoRV32 application and large buffers

**Important**: the SDRAM controller performs a ~200 µs initialization at power-on. Do not access SDRAM before `sdram_wait_ready()` returns.

```cpp
#include "sdram.h"

// Place large variables in SDRAM at link time
// (linker assigns addresses automatically from SDRAM_APP_BASE)
SDRAM_DATA uint8_t  frame_buffer[128 * 64 / 8];  // 1 KB framebuffer
SDRAM_DATA uint32_t modbus_log[4096];             // 16 KB log

int main() {
    // Must be called before any SDRAM access
    sdram_wait_ready();

    // Zero-initialize (SDRAM content is undefined at power-on)
    __builtin_memset(frame_buffer, 0, sizeof(frame_buffer));

    // Direct pointer access (in application region)
    volatile uint32_t *sdram = (volatile uint32_t *)SDRAM_APP_BASE;
    sdram[0] = 0xDEADBEEF;

    // Memory test (application region)
    uint32_t errors = sdram_test(1024);  // Test first 4 KB of app region
    if (errors == 0) uart_puts("SDRAM OK\n");
}
```

**Allocating large buffers dynamically** (pointer arithmetic):
```cpp
// Manual allocator from SDRAM application region
static uintptr_t sdram_ptr = SDRAM_APP_BASE;

void* sdram_alloc(size_t bytes) {
    void* p = (void*)sdram_ptr;
    sdram_ptr += (bytes + 3) & ~3;  // align to 4 bytes
    
    // Safety check: don't allocate beyond 8 MB boundary
    if (sdram_ptr > 0x20800000) return nullptr;
    return p;
}
```

**SDRAM Regions (Memory Map)**:
```
Hardware: 8 MB total (M12L64322A SDRAM on Colorlight i9)
└─ 0x20000000 ─ 0x207FFFFF (8 MB)  ← Application region (PicoRV32)
    ├─ SDRAM_DATA variables (placed by linker)
   ├─ Heap (manual allocation)
   └─ Free space
```

**Constants in `sdram.h`**:
```cpp
#define SDRAM_NODENET_BASE  0x20000000UL    // NodeNet485 reserved start
#define SDRAM_NODENET_SIZE  (1UL * 1024 * 1024)  // 1 MB
#define SDRAM_APP_BASE      0x20100000UL    // Application region start
#define SDRAM_APP_SIZE      (7UL * 1024 * 1024)  // 7 MB available
#define SDRAM_BASE          SDRAM_APP_BASE  // Legacy: now points to app region
#define SDRAM_SIZE          SDRAM_APP_SIZE  // Legacy: 7 MB (not 8 MB)
```

**Accessing NodeNet485 buffers directly** (if needed):
```cpp
// Direct inspection of TX FIFO (should never write here!)
volatile uint8_t *tx_fifo = (volatile uint8_t *)SDRAM_NODENET_BASE;
uint8_t first_msg_dst = tx_fifo[0];

// Direct inspection of RX FIFO
volatile uint8_t *rx_fifo = (volatile uint8_t *)(SDRAM_NODENET_BASE + 0x80000);
uint8_t received_src = rx_fifo[0];
```

---

### I2C0 (`0x10005000`) — via `i2c.h`

`i2c.h` provides blocking helpers that map directly to the MMIO registers.

```cpp
#include "i2c.h"

void i2c_example(void) {
    // Set clock: 400 kHz @ 25 MHz (prescale = 25e6 / (400e3 * 4) = 15)
    i2c0_init(15);

    // Write: send a 2-byte command to device at address 0x3C
    uint8_t cmd[] = { 0x00, 0xAF };     // SSD1306: Co=0, D/C=0, DISPLAY_ON
    int ret = i2c0_write(0x3C, cmd, 2); // returns 0=OK, 1=NACK
    if (ret) uart_puts("I2C NACK!\n");

    // Read: receive 2 bytes from device at 0x48
    uint8_t data[2];
    i2c0_read(0x48, data, 2);

    // Combined write-then-read (register read pattern)
    uint8_t reg = 0x00;
    i2c0_write(0x48, &reg, 1);   // Write register address
    i2c0_read(0x48, data, 2);    // Read register value
}
```

**Raw MMIO access** (for custom protocols):
```cpp
// Wait until cmd FIFO not full, then push a START+WRITE command
while (I2C0_FIFO & I2C_FIFO_CMD_FULL);
I2C0_ADDR = 0x3C;
I2C0_CMD  = I2C_CMD_START | I2C_CMD_WRITE;

// Push data
while (I2C0_FIFO & I2C_FIFO_WR_FULL);
I2C0_DATA = 0xAF;

// Push STOP
I2C0_CMD = I2C_CMD_STOP;

// Wait for completion
i2c0_wait_busy();
```

---

### OLED Display — u8g2 + `u8g2_hal.h`

u8g2 is cloned as a git submodule in `lib/u8g2/`.  
The HAL (`u8g2_hal.h`/`u8g2_hal.cpp`) connects u8g2 to the hardware I2C peripheral.

**Wiring**: SSD1306 OLED → SCL=H4, SDA=G3 (pmodg[0:1]) with 4.7 kΩ pullups to 3.3 V.

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
