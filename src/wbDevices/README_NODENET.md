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
┌─────────────────────────────────────────┐
│      Wishbone Interface (CPU/Bus)       │
├─────────────────────────────────────────┤
│           wb_nodenet (top)              │
├──────────────┬──────────────┬───────────┤
│              │              │           │
│   Encoder    │   Decoder    │ Heartbeat │
│   (TX path)  │   (RX path)  │  Logic    │
│              │              │           │
├──────────────┼──────────────┴───────────┤
│         UART Simple (uart_simple.sv)    │
├─────────────────────────────────────────┤
│    RS-485 UART (rx_i, tx_o, de_o)      │
└─────────────────────────────────────────┘
```

## Module Files

### Core Modules

1. **nodenet_types.sv**
   - Protocol constants (SOH, STX, ETX, EOT, LF)
   - Timing parameters (baud rate divisors, heartbeat interval)
   - Anti-collision backoff timings

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
   - Configurable baud rate (default 115200 @ 25MHz)
   - RS485 driver enable output (de_o)

6. **wb_nodenet.sv**
   - Main integration module
   - Wishbone B.4 interface
   - Orchestrates TX/RX state machine
   - Register map and control

## Wishbone Register Map

Base address: `0x10006000`

| Offset | Name       | R/W | Bits  | Purpose                              |
|--------|-----------|-----|-------|--------------------------------------|
| 0x00   | TX_CMD    | W   | 31:0  | TX command: [dst(8), len(16)]        |
| 0x04   | TX_DATA   | W   | 7:0   | TX data byte (auto-enqueue)          |
| 0x08   | RX_DATA   | R   | 31:0  | RX data: [src(8), len(16)] header or data byte |
| 0x0C   | STATUS    | R   | 31:0  | [RX_count(8), TX_count(8), TX_ready(1), RX_valid(1)] |
| 0x10   | CONFIG    | R/W | 31:0  | [addr(8), priority(2), hb_interval(20)] |

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
 ├─ [heartbeat_trigger] → generate heartbeat message
 ├─ [app calls send()] → queue to TX FIFO
 │
WAITING_TRANSMISSION
 ├─ Broadcast (dst==0) → delay addr × 50ms
 └─ Unicast         → delay 10ms
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
 │  └─ Push to RX FIFO
 │
IDLE (searching for next SOH)
```

## Firmware Interface (C++)

Header: `include/nodenet.h`

```cpp
// Initialize (address, priority)
nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);

// Send unicast
nodenet0_send(0x02, "Hello", 5);

// Send broadcast
nodenet0_broadcast("Alert!");

// Check for messages
if (nodenet0_has_message()) {
  NodeNetMessage msg = nodenet0_read();
  printf("From %d: %.*s\n", msg.src_addr, msg.len, msg.data);
  nodenet0_free_message(msg);
}

// Check message count
uint8_t count = nodenet0_message_count();
```

## Timing Parameters (@ 25 MHz)

| Parameter | Duration | Cycles |
|-----------|----------|--------|
| Heartbeat (default) | 10 seconds | 250,000,000 |
| Broadcast backoff per addr unit | 50 ms | 1,250,000 |
| Unicast delay per addr unit | 2 ms | 50,000 |
| Line ready time | 10 ms | 250,000 |
| Receive timeout | 1 second | 25,000,000 |

**Anti-collision mechanism**: If node address is 0x05:
- After unicast: can transmit again after 5 × 2ms = 10ms
- After broadcast: must wait 5 × 50ms = 250ms before next transmission

This prevents medium contention when multiple nodes transmit.

## Integration Steps

1. **Add to top.sv**:
   ```verilog
   wb_nodenet nodenet0_inst (
     .clk_i(clk25),
     .rst_i(rst),
     .adr_i(wb_adr),
     .dat_i(wb_dat_w),
     .dat_o(wb_nodenet_dat),
     .we_i(wb_we),
     .stb_i(wb_stb && wb_nodenet_sel),
     .cyc_i(wb_cyc),
     .ack_o(wb_nodenet_ack),
     .uart_rx_i(uart1_rx),
     .uart_tx_o(uart1_tx),
     .uart_de_o(uart1_de)
   );
   ```

2. **Add address decoder**:
   ```verilog
   assign wb_nodenet_sel = (wb_adr >= 32'h10006000) && (wb_adr < 32'h10006020);
   ```

3. **Firmware usage** (see above)

## Performance Notes

- **Throughput**: Limited by UART baud rate (115200 @ 115.2 kbps)
- **Message latency**: ~100ms for 64-byte message (with encoding + framing)
- **Anti-collision effective**: Prevents collisions up to ~20 nodes on shared RS-485 bus
- **Power**: Minimal (idle state is just heartbeat counter)

## Known Limitations

1. **No flow control**: Sender doesn't check if receiver is ready
2. **Fixed frame size**: No support for variable MTU
3. **Simple CRC**: XOR is weak; real protocols use CRC-16 or better
4. **Single priority level at a time**: No queue priority mixing
5. **Blocking receive**: Firmware must poll for messages (no interrupt yet)

## Future Enhancements

- [ ] Interrupt-driven reception
- [ ] CRC-16 polynomial
- [ ] Configurable baud rate via CONFIG register
- [ ] FIFO depth configuration
- [ ] DMA support for large payloads
- [ ] Flow control (RTS/CTS)

## Testing

See [../README.md](../README.md) for system-level tests and examples.
