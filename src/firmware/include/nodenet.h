/**
 * @file nodenet.h
 * @brief NodeNet485 Wishbone Driver (MMIO Mailbox)
 * 
 * Overview
 * ════════
 * This is the firmware API for the NodeNet485 multi-node RS-485 protocol.
 * Hardware exposes a mailbox register model over Wishbone:
 *   - TX staging registers (destination, length, payload bytes)
 *   - RX header/data reader for one decoded message at a time
 * Framing/CRC/encoding are handled in RTL.
 * 
 * Quick Start
 * ═══════════
 *   constexpr uint32_t NODENET0_BASE = 0x10006000u;
 *   NodeNet nodenet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);
 *   nodenet.Send(0x02, "Hello", 5);               // Send to node 0x02
 *   if (nodenet.HasMessage()) {
 *     NodeNetMessage msg = nodenet.ReadMessage();   // Read incoming message
 *     process(msg.src_addr, msg.data, msg.len);    // Process message
 *     NodeNet::FreeMessage(msg);                   // Deallocate
 *   }
 * 
 * Mailbox Register Flow
 * ═════════════════════
 * TX path:
 *   1. Write TX_CMD = [dst | len]
 *   2. Write payload bytes to TX_DATA
 *   3. Trigger CONTROL.bit0
 *   4. Hardware schedules/encodes/transmits
 *
 * RX path:
 *   1. Poll RX valid bit (RX_HDR[16])
 *   2. Read src/len from RX_HDR
 *   3. Read len bytes from RX_DATA
 *   4. Mailbox auto-clears when last byte is read
 * 
 * Debugging Tips
 * ══════════════
 * 1. Inspect status register at [base + NODENET_STATUS_OFS] for RX/TX/error bits
 * 2. Inspect RX header at [base + NODENET_RX_HDR_OFS] for [src, valid, len]
 * 3. Monitor wire protocol on G5 (RX), D16 (TX)
 * 4. Inject known frames and verify CRC/error flags
 */

#ifndef NODENET_H
#define NODENET_H

#include <cstdint>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
// Hardware Registers (Wishbone B.4 Slave at 0x10006000)
// ═══════════════════════════════════════════════════════════════════════════

// Register offsets
#define NODENET_TX_CMD_OFS   0x00u
#define NODENET_TX_DATA_OFS  0x04u
#define NODENET_RX_HDR_OFS   0x08u
#define NODENET_RX_DATA_OFS  0x0Cu
#define NODENET_CONFIG_OFS   0x10u
#define NODENET_CONTROL_OFS  0x14u
#define NODENET_STATUS_OFS   0x18u
#define NODENET_LED_CFG_OFS  0x1Cu
#define NODENET_UART_BAUD_OFS 0x20u

#define NODENET_MAX_PAYLOAD_SIZE 2048u

#define NODENET_STATUS_RX_OVERFLOW       (1u << 31)
#define NODENET_STATUS_RX_ERROR          (1u << 30)
#define NODENET_STATUS_HEARTBEAT_DUE     (1u << 29)
#define NODENET_STATUS_TX_DELAY_ACTIVE   (1u << 28)
#define NODENET_STATUS_TX_PENDING        (1u << 27)
#define NODENET_STATUS_TX_ACTIVE         (1u << 26)
#define NODENET_STATUS_UART_READY        (1u << 25)
#define NODENET_STATUS_RX_VALID          (1u << 24)
#define NODENET_STATUS_TX_STAGE_VALID    (1u << 23)
#define NODENET_STATUS_TX_BUFFER_FULL    (1u << 22)
#define NODENET_STATUS_RX_UART_SEEN      (1u << 18)
#define NODENET_STATUS_RX_UART_FRAME_ERR (1u << 17)

// ═══════════════════════════════════════════════════════════════════════════
// Protocol Constants
// ═══════════════════════════════════════════════════════════════════════════

#define NODENET_BROADCAST 0x00  // Address 0 means broadcast to all nodes

// Message structure returned by NodeNet::ReadMessage()
struct NodeNetMessage {
  uint8_t src_addr;             // Sender's node address
  uint16_t len;                 // Payload length in bytes
  uint8_t* data;                // Dynamically allocated payload buffer
};

// Priority levels (affects transmission scheduling)
enum NodeNetPriority {
  NODENET_PRIORITY_LOW = 0,     // Lowest: Sent last
  NODENET_PRIORITY_NORMAL = 1,  // Default: Normal scheduling
  NODENET_PRIORITY_HIGH = 2     // Highest: Sent first
};

class NodeNet {
public:
  using StatusCallback = void (*)(uint8_t line, const char* text);

  explicit NodeNet(uint32_t base,
                   uint8_t addr,
                   uint32_t uart_baud,
                   NodeNetPriority priority,
                   uint32_t led_blink_ms = 100u)
      : base_(base) {
    Init(addr, uart_baud, priority, led_blink_ms);
  }

  void Init(uint8_t addr, uint32_t uart_baud, NodeNetPriority priority, uint32_t led_blink_ms = 100u) {
    node_addr_ = addr;
    uart_baud_ = uart_baud;
    priority_ = priority;
    Write(NODENET_UART_BAUD_OFS, ComputeUartDivisor(uart_baud));
    Write(NODENET_CONFIG_OFS, ((uint32_t)priority << 8) | (uint32_t)addr);
    Write(NODENET_LED_CFG_OFS, led_blink_ms);
    Write(NODENET_CONTROL_OFS, 0x2u);
  }

  uint32_t Status() const {
    return Read(NODENET_STATUS_OFS);
  }

  bool TxMailboxReady() const {
    uint32_t status = Status();
    return (status & (NODENET_STATUS_TX_STAGE_VALID | NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE)) == 0;
  }

  bool TxHasSpace(uint16_t msg_len) const {
    return msg_len <= NODENET_MAX_PAYLOAD_SIZE && TxMailboxReady();
  }

  bool HasMessage() const {
    return (Status() & NODENET_STATUS_RX_VALID) != 0u;
  }

  uint8_t MessageCount() const {
    return HasMessage() ? 1 : 0;
  }

  void Send(uint8_t dst, const uint8_t* data, uint16_t len) const {
    if (len > NODENET_MAX_PAYLOAD_SIZE) {
      len = NODENET_MAX_PAYLOAD_SIZE;
    }

    while (!TxHasSpace(len)) {
    }

    Write(NODENET_TX_CMD_OFS, ((uint32_t)dst << 24) | (uint32_t)len);
    for (uint16_t index = 0; index < len; ++index) {
      Write(NODENET_TX_DATA_OFS, data[index]);
    }
    Write(NODENET_CONTROL_OFS, 0x1u);
  }

  void Send(uint8_t dst, const char* str) const {
    Send(dst, (const uint8_t*)str, strlen(str));
  }

  void Broadcast(const uint8_t* data, uint16_t len) const {
    Send(NODENET_BROADCAST, data, len);
  }

  void Broadcast(const char* str) const {
    Send(NODENET_BROADCAST, str);
  }

  NodeNetMessage ReadMessage() const {
    uint32_t header = Read(NODENET_RX_HDR_OFS);
    NodeNetMessage msg;

    const bool valid = ((header >> 16) & 0x1u) != 0u;
    uint16_t hw_len = (uint16_t)(header & 0xFFFFu);

    if (!valid) {
      msg.src_addr = 0u;
      msg.len = 0u;
      msg.data = new uint8_t[1];
      msg.data[0] = 0u;
      return msg;
    }

    if (hw_len > NODENET_MAX_PAYLOAD_SIZE) {
      hw_len = NODENET_MAX_PAYLOAD_SIZE;
    }

    msg.src_addr = (uint8_t)(header >> 24);
    msg.len = hw_len;
    msg.data = new uint8_t[(uint32_t)msg.len + 1u];

    for (uint16_t index = 0; index < msg.len; ++index) {
      msg.data[index] = (uint8_t)Read(NODENET_RX_DATA_OFS);
    }

    // Defensive: ensure C-string termination regardless of payload content.
    msg.data[msg.len] = 0u;

    return msg;
  }

  static void FreeMessage(NodeNetMessage& msg) {
    delete[] msg.data;
    msg.data = nullptr;
    msg.len = 0;
  }

    bool test(StatusCallback callback = nullptr) {
      const uint32_t tx_busy_mask =
        (NODENET_STATUS_TX_DELAY_ACTIVE | NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE);

      const uint32_t div_read = Read(NODENET_UART_BAUD_OFS) & 0x000F'FFFFu;
      const uint32_t div_expected = ComputeUartDivisor(uart_baud_);
      const bool baud_ok = (div_read == div_expected);
      if (callback) {
        callback(0, baud_ok ? "[NN] Baud cfg OK" : "[NN] Baud cfg FAIL");
      }

      // bit1: clear RX/error state, bit2: heartbeat trigger
      Write(NODENET_CONTROL_OFS, 0x6u);

      bool saw_tx_activity = false;
      bool tx_returned_idle = false;
      uint32_t status_snapshot = Read(NODENET_STATUS_OFS);

      // Wait up to ~4.5s for first activity (addr=0x41 has ~3.25s backoff).
      uint32_t wait_activity_until = millis() + 4500u;
      while ((int32_t)(millis() - wait_activity_until) < 0) {
        status_snapshot = Read(NODENET_STATUS_OFS);
        if ((status_snapshot & tx_busy_mask) != 0u) {
          saw_tx_activity = true;
          break;
        }
      }

      if (saw_tx_activity) {
        // Wait for full cycle completion.
        uint32_t wait_idle_until = millis() + 7000u;
        while ((int32_t)(millis() - wait_idle_until) < 0) {
          status_snapshot = Read(NODENET_STATUS_OFS);
          if ((status_snapshot & tx_busy_mask) == 0u) {
            tx_returned_idle = true;
            break;
          }
        }
      }

      if (callback) {
        callback(1, saw_tx_activity ? "[NN] TX activity OK" : "[NN] TX activity FAIL");
        callback(2, tx_returned_idle ? "[NN] TX idle OK" : "[NN] TX idle FAIL");
      }

      if (callback && (!saw_tx_activity || !tx_returned_idle)) {
        char status_hex[11] = {};
        FormatHex32(status_snapshot, status_hex);
        callback(3, "[NN] status:");
        callback(4, status_hex);
      }

      const bool rx_ok = (status_snapshot & (NODENET_STATUS_RX_ERROR | NODENET_STATUS_RX_OVERFLOW)) == 0u;
      return baud_ok && saw_tx_activity && tx_returned_idle && rx_ok;
  }


private:
  uint32_t base_;
  uint8_t node_addr_ = 0;
  uint32_t uart_baud_ = 0;
  NodeNetPriority priority_ = NODENET_PRIORITY_NORMAL;

  static uint32_t ComputeUartDivisor(uint32_t baudrate) {
    if (baudrate == 0u) {
      return 1u;
    }

    uint32_t divisor = (25000000u + (baudrate / 2u)) / baudrate;
    if (divisor == 0u) {
      divisor = 1u;
    }
    if (divisor > 0xFFFFFu) {
      divisor = 0xFFFFFu;
    }
    return divisor;
  }

  static void FormatHex32(uint32_t value, char* out) {
    static const char kHex[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (uint32_t i = 0; i < 8; ++i) {
      uint32_t nib = (value >> ((7u - i) * 4u)) & 0xFu;
      out[2u + i] = kHex[nib];
    }
    out[10] = '\0';
  }

  uint32_t Read(uint32_t offset) const {
    return *(volatile uint32_t*)(base_ + offset);
  }

  void Write(uint32_t offset, uint32_t value) const {
    *(volatile uint32_t*)(base_ + offset) = value;
  }
};

#endif  // NODENET_H
