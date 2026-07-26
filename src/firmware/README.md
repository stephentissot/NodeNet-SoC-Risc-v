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
├── u8g2_hal.h       u8g2 HAL declarations
├── u8g2_hal.cpp     u8g2 HAL: I2C callback + delay callback
└── lib/
    └── u8g2/        u8g2 graphics library (git submodule)
        └── csrc/    132 C source files compiled with --gc-sections
```

---

## Memory Map

| Address Range | Size | Description |
|--------------|------|-------------|
| `0x00000000–0x0000FFFF` | 64 KiB | **Boot ROM** — firmware binary |
| `0x00010000–0x0002FFFF` | 64 KiB | **RAM** — stack, BSS, initialized data |
| `0x10000000` | 4 B | **LED GPIO** |
| `0x10001000` | 12 B | **UART0** |
| `0x10005000` | 32 B | **I2C0** |
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

### UART0 (`0x10001000`)

16-byte RX and TX FIFOs. Default baud: `prescale=27` ≈ 115200 @ 25 MHz.

```cpp
#define UART0_DATA   (*(volatile uint32_t*)0x10001000)
#define UART0_STATUS (*(volatile uint32_t*)0x10001004)
#define UART0_BAUD   (*(volatile uint32_t*)0x10001008)

// Status bits
#define UART_RX_EMPTY (1u << 0)
#define UART_RX_FULL  (1u << 1)
#define UART_TX_EMPTY (1u << 2)
#define UART_TX_FULL  (1u << 3)
#define UART_RX_OVERRUN  (1u << 4)
#define UART_RX_FRAMEERR (1u << 5)

void uart_init(uint16_t prescale) {
    UART0_BAUD = prescale;          // 27 → ~115200 baud @ 25 MHz
}

void uart_putc(char c) {
    while (UART0_STATUS & UART_TX_FULL);
    UART0_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

char uart_getc(void) {
    while (UART0_STATUS & UART_RX_EMPTY);
    return (char)(UART0_DATA & 0xFF);
}

bool uart_available(void) {
    return !(UART0_STATUS & UART_RX_EMPTY);
}

// Echo loop example
void uart_echo_loop(void) {
    uart_init(27);
    uart_puts("Ready\n");
    for (;;) {
        if (uart_available()) {
            char c = uart_getc();
            uart_putc(c);
        }
        // Clear error flags (sticky, write 1 to clear)
        if (UART0_STATUS & (UART_RX_OVERRUN | UART_RX_FRAMEERR))
            UART0_STATUS = UART_RX_OVERRUN | UART_RX_FRAMEERR;
    }
}
```

---

### SDRAM (`0x20000000`, 8 MB)

**Important**: the SDRAM controller performs a ~200 µs initialization at power-on. Do not access SDRAM before `sdram_wait_ready()` returns.

```cpp
#include "sdram.h"

// Place large variables in SDRAM at link time
// (linker assigns addresses automatically from 0x20000000)
SDRAM_DATA uint8_t  frame_buffer[128 * 64 / 8];  // 1 KB framebuffer
SDRAM_DATA uint32_t modbus_log[4096];             // 16 KB log

int main() {
    // Must be called before any SDRAM access
    sdram_wait_ready();

    // Zero-initialize (SDRAM content is undefined at power-on)
    __builtin_memset(frame_buffer, 0, sizeof(frame_buffer));

    // Direct pointer access
    volatile uint32_t *sdram = (volatile uint32_t *)SDRAM_BASE;
    sdram[0] = 0xDEADBEEF;

    // Memory test
    uint32_t errors = sdram_test(1024);  // Test first 4 KB
    if (errors == 0) uart_puts("SDRAM OK\n");
}
```

**Allocating large buffers dynamically** (pointer arithmetic):
```cpp
// Manual allocator from SDRAM (simple bump allocator)
static uintptr_t sdram_ptr = SDRAM_BASE;

void* sdram_alloc(size_t bytes) {
    void* p = (void*)sdram_ptr;
    sdram_ptr += (bytes + 3) & ~3;  // align to 4 bytes
    return p;
}
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
