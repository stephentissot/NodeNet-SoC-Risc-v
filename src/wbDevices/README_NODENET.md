# NodeNet485 Wishbone Module

## Overview

`wb_nodenet.sv` implements the NodeNet485 protocol as a Wishbone B.4 slave peripheral. NodeNet485 is a simple, robust message-passing protocol over RS-485 UART with:

- **Framing**: HDLC-style (SOH, STX, ETX, EOT markers)
- **Encoding**: 2x payload expansion with embedded parity bits per nibble
- **Error Detection**: XOR CRC
- **Anti-collision**: Address-based backoff for broadcasts
- **Heartbeat**: Periodic keep-alive messages
- **Flow Control**: Priority-based transmission (LOW/NORMAL/HIGH)

## Architecture

```
┌────────────────────────────────────────────────────────┐
│        Wishbone B.4 Interface (CPU/Bus)                │
│  Registers: TX_CMD, TX_DATA, RX_HDR, RX_DATA, STATUS  │
└────────────────────────────────────────────────────────┘
                    ↓
┌────────────────────────────────────────────────────────┐
│         wb_nodenet.sv (Mailbox Transport)              │
│  - Stages one TX message from firmware                 │
│  - Buffers one decoded RX message for firmware         │
│  - Handles heartbeat generation                       │
│  - Schedules anti-collision delay before TX           │
└────────────────────────────────────────────────────────┘
                    ↓
┌────────────────────────────────────────────────────────┐
│           Encoder/Decoder FSMs                          │
│  - Payload encoding with nibble parity                │
│  - Protocol framing (SOH/STX/ETX/EOT)                 │
│  - CRC validation                                      │
└────────────────────────────────────────────────────────┘
                    ↓
┌────────────────────────────────────────────────────────┐
│        uart_simple.sv (8N1 UART @ 1 Mb/s)             │
│  Configurable baud rate (default: 1_000_000)          │
└────────────────────────────────────────────────────────┘
                    ↓
┌────────────────────────────────────────────────────────┐
│    RS-485 Transceiver (Hardware Module)                │
│    rx_i (H16) ← Data from bus                         │
│    tx_o (H17) → Data to bus                           │
│    DE automatic (module handles driver enable)        │
└────────────────────────────────────────────────────────┘
```

## Mailbox Transport Model

The current implementation is intentionally **self-contained**: `wb_nodenet.sv` has no SDRAM master port, so it exposes a small mailbox-style transport over Wishbone instead of pretending to use external memory.

**TX mailbox:**
- Firmware writes one header to `TX_CMD`
- Firmware streams payload bytes through `TX_DATA`
- Firmware sets `CONTROL.bit0` to schedule transmission

**RX mailbox:**
- Hardware decodes and validates the next received frame
- Header appears in `RX_HDR`
- Payload bytes are drained from `RX_DATA`
- Reading the last byte automatically frees the RX mailbox

**Capacity:**
- One staged TX message at a time
- One staged RX message at a time
- Maximum payload length: 2048 bytes

## SDRAM Usage Clarification

`wb_nodenet.sv` does **not** read or write external SDRAM.

- No TX FIFO in SDRAM
- No RX FIFO in SDRAM
- No concurrent SDRAM access path from NodeNet hardware

All NodeNet message buffering is internal to `wb_nodenet.sv` and exposed through
MMIO mailbox registers only.

## Module Files

### Core Modules

1. **nodenet_defines.vh**
   - Protocol constants as Verilog macros (SOH, STX, ETX, EOT, LF)
   - Timing parameters (baud rate divisors, heartbeat interval)
   - Anti-collision backoff timings
   - Included by all nodenet_*.sv modules

2. **nodenet_encoder.sv**
   - Converts application messages → wire protocol
   - Encodes payload with nibble parity (2x expansion)
   - Computes XOR CRC
   - Outputs byte-by-byte on strobed interface

3. **nodenet_decoder.sv**
   - Byte-by-byte reception & state machine
   - Parity check on each encoded nibble
   - CRC validation
   - Filters by destination (own address or broadcast 0)

4. **nodenet_heartbeat.sv**
   - Tracks heartbeat timer
   - Calculates anti-collision backoff based on:
     - `addr * 50ms` for broadcasts (avoid collisions)
     - `addr * 2ms` for unicasts (stagger transmissions)
   - Signals when heartbeat is due

5. **uart_simple.sv**
   - Basic 8N1 UART (no flow control)
   - Configurable baud rate (default: 1 Mb/s @ 25MHz = 24 cycles per bit)
   - No external driver control needed (RS485 module handles DE automatically)

6. **wb_nodenet.sv**
   - Main integration module
   - Wishbone B.4 interface
   - Orchestrates TX/RX state machine and mailbox registers
   - Register map and control

## Wishbone Register Map

Base address: `0x10006000`

| Offset | Name       | R/W | Bits  | Purpose                              |
|--------|-----------|-----|-------|--------------------------------------|
| 0x00   | TX_CMD    | R/W | 31:0  | `[dst(31:24) | len(15:0)]`           |
| 0x04   | TX_DATA   | R/W | 7:0   | Write payload bytes / read load count |
| 0x08   | RX_HDR    | R   | 31:0  | `[src(31:24) | rx_valid(16) | len]`  |
| 0x0C   | RX_DATA   | R   | 7:0   | Read next received payload byte      |
| 0x10   | CONFIG    | R/W | 31:0  | `[hb_interval(31:10) | prio(9:8) | addr]` |
| 0x14   | CONTROL   | W   | 2:0   | `bit0=trigger_tx bit1=clear_rx bit2=queue_heartbeat` |
| 0x18   | STATUS    | R   | 31:0  | TX/RX state, UART ready, error flags |
| 0x1C   | LED_CFG   | R/W | 31:0  | TX/RX activity LED pulse duration (milliseconds) |

**Key STATUS bits:**
- `bit31`: RX overflow
- `bit30`: RX decode or timeout error
- `bit27`: TX pending after software trigger
- `bit26`: TX active on the wire
- `bit25`: UART ready for the next encoded byte
- `bit24`: RX mailbox contains a complete decoded frame

## Protocol Wire Format

### Message Frame

```
3×LF + SOH + DST(8) + SRC(8) + LEN_HI(8) + LEN_LO(8) + STX + PAYLOAD_ENCODED + ETX + CRC(8) + EOT + 2×LF
```

### Payload Encoding

Each byte encodes as 2 nibbles with parity bits:

```
Original byte:    B₇ B₆ B₅ B₄ B₃ B₂ B₁ B₀

High nibble out:  B₇ B₆ B₅ B₄ ¬B₇ ¬B₆ ¬B₅ ¬B₄
Low nibble out:   B₃ B₂ B₁ B₀ ¬B₃ ¬B₂ ¬B₁ ¬B₀

Property: high_nibble & low_nibble == 0 (always valid encoded)
```

### Heartbeat Frame (Special Case)

```
3×LF + SOH + 0x00(dst=broadcast) + SRC(8) + 0x00 + 0x00 + EOT + [crc=SRC] + LF + LF
```

No payload, no STX/ETX, just immediate EOT after length.

## State Machine

### Transmit Path

```
IDLE
 ├─ [heartbeat_trigger] → queue heartbeat frame
 ├─ [app calls send()] → stage TX mailbox
 │
WAITING_TRANSMISSION
 ├─ Broadcast (dst==0) → delay addr × 50ms
 └─ Unicast         → delay 10ms + addr × 2ms
 │
TRANSMITTING
 └─ Send encoded bytes via UART
 │
WAIT_SEND_COMPLETE
 └─ uart.flush() → release RS485 driver
 │
[reset heartbeat timer] → IDLE
```

### Receive Path

```
IDLE (searching for SOH)
 ├─ [byte == SOH]          → RX_DST
 │
RX_DST/SRC/LEN/STX     (header parsing)
 │
RX_PAYLOAD             (nibble decoding with parity check)
 │
RX_CRC                 (read CRC byte)
 │
RX_EOT                 (expect 0x04)
 │
VALIDATE
 ├─ [CRC match && (dst == my_addr || dst == 0)]
 │  └─ Publish RX mailbox
 │
IDLE (searching for next SOH)
```

## Firmware Interface (C++)

Header: `include/nodenet.h`

```cpp
// Preferred object API
NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);

// Send unicast
myNodeNet.Send(0x02, "Hello", 5);

// Send broadcast
myNodeNet.Broadcast("Alert!");

// Check for messages
if (myNodeNet.HasMessage()) {
   NodeNetMessage msg = myNodeNet.ReadMessage();
  printf("From %d: %.*s\n", msg.src_addr, msg.len, msg.data);
   NodeNet::FreeMessage(msg);
}

// Check message count
uint8_t count = myNodeNet.MessageCount();
```

## Timing Parameters (@ 25 MHz)

| Parameter | Duration | Cycles | Divisor Value |
|-----------|----------|--------|---------------|
| UART Bit Time (1 Mb/s) | 1 µs | 25 | `BAUD_RATE=1_000_000` |
| Heartbeat (default) | 10 seconds | 250,000,000 | Configurable |
| Broadcast backoff per addr unit | 50 ms | 1,250,000 | Per node address |
| Unicast delay per addr unit | 2 ms | 50,000 | Per node address |
| Line ready time | 10 ms | 250,000 | Hardcoded |
| Receive timeout | 1 second | 25,000,000 | Hardcoded |

**Anti-collision mechanism**: If node address is 0x05:
- After unicast: can transmit again after 5 × 2ms = 10ms
- After broadcast: must wait 5 × 50ms = 250ms before next transmission

This prevents medium contention when multiple nodes transmit.

## Integration Steps (Current State - Colorlight i9)

### Hardware Configuration

**Pin assignments** (already configured in constraints):
- **RX**: Colorlight i9 pin H16 (input from RS485 module)
- **TX**: Colorlight i9 pin H17 (output to RS485 module)
- **Driver Enable**: Not required (RS485 module handles automatically)

**FPGA Wishbone Integration** (already in src/top.sv):

```verilog
wb_nodenet #(
   .CLOCK_RATE(25_000_000)
) nodenet0 (
    .clk_i(clk_25mhz),
    .rst_i(reset),
    
    .adr_i(wb_adr),
    .dat_i(wb_dat_o),
    .dat_o(nodenet_dat),
    .we_i(wb_we),
    .stb_i(wb_nodenet_sel),
    .cyc_i(wb_nodenet_sel),
    .ack_o(nodenet_ack),
    
   .uart_rx_i(rx0),      // H16
   .uart_tx_o(tx0),      // H17
   .tx_led_o(led_h18),   // H18 (TX activity pulse)
   .rx_led_o(led_g18)    // G18 (RX default ON, pulse on RX)
);

// Note: TX/RX LED pins are configured active-low with pull-up in LPF.
// This keeps LEDs off during startup unless logic actively drives them low.

assign wb_nodenet_sel = wb_cyc && wb_stb && (wb_adr[31:12] == NODENET_BASE[31:12]);
```

### Firmware Usage

Firmware API is in `src/firmware/include/nodenet.h`:

```cpp
class NodeNet {
public:
   explicit NodeNet(uint32_t base, uint8_t addr, NodeNetPriority priority, uint32_t led_blink_ms = 100u);
   void Init(uint8_t addr, NodeNetPriority priority, uint32_t led_blink_ms = 100u);
   uint32_t Status() const;
   bool HasMessage() const;
   uint8_t MessageCount() const;
   void Send(uint8_t dst, const uint8_t* data, uint16_t len) const;
   void Send(uint8_t dst, const char* str) const;
   void Broadcast(const uint8_t* data, uint16_t len) const;
   void Broadcast(const char* str) const;
   NodeNetMessage ReadMessage() const;
   static void FreeMessage(NodeNetMessage& msg);
};

// Compatibility wrappers retained for existing code:
static inline void nodenet0_init(uint8_t my_addr, NodeNetPriority priority, uint32_t led_blink_ms = 100u);

// Send unicast
static inline void nodenet0_send(uint8_t dst, const uint8_t* data, uint16_t len);
static inline void nodenet0_send(uint8_t dst, const char* str);  // C-string overload

// Send broadcast
static inline void nodenet0_broadcast(const uint8_t* data, uint16_t len);
static inline void nodenet0_broadcast(const char* str);  // C-string overload

// Receive
static inline bool nodenet0_has_message();
static inline uint8_t nodenet0_message_count();
static inline NodeNetMessage nodenet0_read();
static inline void nodenet0_free_message(NodeNetMessage& msg);
```

### Example Implementation (main.cpp)

Current test code demonstrates:
- Boot LED blink pattern (3 blinks)
- Message listening loop
- Echo response on received unicast messages
- LED heartbeat indicator (blink every ~1 second)

For custom applications:
1. Construct `NodeNet myNodeNet(NODENET0_BASE, node_address, priority, led_blink_ms)` at startup
2. Check `myNodeNet.HasMessage()` in main loop
3. Process messages with `myNodeNet.ReadMessage()`
4. Send replies with `myNodeNet.Send(sender_addr, ...)`

### Previous Integration Instructions (Preserved for Reference)

To add to a new system:

1. **Add to top.sv**:
   ```verilog
   wb_nodenet nodenet0_inst (
     .clk_i(clk),
     .rst_i(rst),
     .adr_i(wb_adr),
     .dat_i(wb_dat_w),
     .dat_o(wb_nodenet_dat),
     .we_i(wb_we),
     .stb_i(wb_stb && wb_nodenet_sel),
     .cyc_i(wb_cyc),
     .ack_o(wb_nodenet_ack),
     .uart_rx_i(uart_rx),
     .uart_tx_o(uart_tx)
   );
   ```

2. **Add address decoder**:
   ```verilog
   assign wb_nodenet_sel = (wb_adr >= 32'h10006000) && (wb_adr < 32'h10006020);
   ```

3. **Add to Wishbone mux**:
   ```verilog
   assign wb_dat_i = nodenet_ack ? nodenet_dat : ...other_devices...;
   assign wb_ack = nodenet_ack | ...other_acks...;
   ```

## Performance Notes

- **Throughput**: Limited by UART baud rate (1 Mb/s = 125 kB/s raw)
  - Effective payload: ~40-50 kB/s after encoding overhead (2x expansion)
- **Message latency**: ~10ms for 64-byte message (at 1 Mb/s with encoding + framing)
- **Anti-collision effective**: Prevents collisions up to ~20 nodes on shared RS-485 bus
- **Power**: Minimal (idle state is just heartbeat counter + UART clock gating)
- **Jitter**: Sub-millisecond (no OS, direct hardware timing)

## Known Limitations

1. **Loopback Mode Active**: Current wb_nodenet.sv implements simple RX→TX echo (not full encoder/decoder FSM)
   - Sufficient for hardware integration validation
   - Full protocol state machine ready for implementation in encoder/decoder modules

2. **No TX/RX FIFOs Yet**: Currently single-message mode only
   - FIFO infrastructure ready in module stubs
   - Will improve throughput in burst scenarios

3. **No Interrupt Support**: Firmware must poll for messages
   - `irq_o` port available on wb_nodenet for future use

4. **No Flow Control**: Sender doesn't check if receiver is ready
   - Acceptable for low-bandwidth protocols
   - RTS/CTS can be added to future iterations

5. **Simple CRC**: XOR is weak; real protocols use CRC-16 or better

## Future Enhancements

- [ ] Complete encoder/decoder instantiation (currently in modules, not wired)
- [ ] Full TX/RX FIFO queues (32-entry hardware queues ready)
- [ ] Interrupt-driven reception (hardware support exists)
- [ ] CRC-16 polynomial (more robust error detection)
- [ ] Configurable baud rate via CONFIG register (UART supports parameterized rate)
- [ ] DMA support for large payloads
- [ ] Flow control (RTS/CTS) on RS-485 lines

## Testing

See [../README.md](../README.md) for system-level tests and examples.
