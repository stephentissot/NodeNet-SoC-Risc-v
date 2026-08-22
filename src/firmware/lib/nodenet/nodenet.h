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
 *   NodeNet nodenet(NODENET0_BASE, 0x01, 1'000'000, 200, nullptr, nullptr);
 *   nodenet.Send(0x02, "Hello", 5);               // Send to node 0x02
 *   if (nodenet.HasMessage()) {
 *     NodeNetMessage msg = nodenet.ReadMessage();   // Read incoming message
 *     process(msg.src_addr, msg.data, msg.len);    // Process message
 *     NodeNet::FreeMessage(msg);                   // Free heap buffer from ReadMessage()
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
 *   1. Poll RX valid bit (RX_HDR[15])
 *   2. Read src/dst/len from RX_HDR
 *   3. Read len bytes from RX_DATA
 *   4. Mailbox auto-clears when last byte is read
 * 
 * Debugging Tips
 * ══════════════
 * 1. Inspect status register at [base + NODENET_STATUS_OFS] for RX/TX/error bits
 * 2. Inspect RX header at [base + NODENET_RX_HDR_OFS] for [src, dst, valid, len]
 * 3. Monitor wire protocol on G5 (RX), D16 (TX)
 * 4. Inject known frames and verify CRC/error flags
 */

#ifndef NODENET_H
#define NODENET_H

#include <cstdint>
#include <cstring>
#include "bigsister.h"
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
#define NODENET_IRQ_CTRL_OFS 0x24u

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

// Message structure used by polled reads and IRQ callbacks.
// Ownership depends on the source:
// - ReadMessage(): data points to a heap buffer owned by the caller, free with FreeMessage().
// - IRQ callback path: data points to a shared internal IRQ buffer, copy it immediately and do not free it.
struct NodeNetMessage {
  uint8_t src_addr = 0u;        // Sender's node address
  uint8_t dest_addr = 0u;       // Destination node address (0 = broadcast)
  bool broadcast = false;       // True when dest_addr == 0
  uint16_t len = 0u;            // Payload length in bytes
  uint8_t* data = nullptr;      // Payload storage, owned either by caller or the IRQ path depending on source
};

class NodeNet {
public:
  using StatusCallback = void (*)(uint8_t line, const char* text);
  using MessageCallback = void (*)(const NodeNetMessage& msg);

  explicit NodeNet(uint32_t base,
                   uint8_t addr,
                   uint32_t uart_baud,
                   uint32_t led_blink_ms = 100u,
                   MessageCallback broadcastCallback = nullptr,
                   MessageCallback messageCallback = nullptr);

  void Init(uint8_t addr,
            uint32_t uart_baud,
            uint32_t led_blink_ms = 100u,
            MessageCallback broadcastCallback = nullptr,
            MessageCallback messageCallback = nullptr);

  uint32_t Status() const;

  bool TxMailboxReady() const;

  bool TxHasSpace(uint16_t msg_len) const;

  bool HasMessage() const;

  uint8_t MessageCount() const;

  void Send(uint8_t dst, const uint8_t* data, uint16_t len) const;

  void Send(uint8_t dst, const char* str) const;

  void Broadcast(const uint8_t* data, uint16_t len) const;

  void Broadcast(const char* str) const;

  void SetCallbacks(MessageCallback broadcastCallback,
                    MessageCallback messageCallback);

  NodeNetMessage ReadMessage() const;

  // Free only messages produced by ReadMessage().
  // IRQ callback messages use an internal shared buffer and must not be freed.
  static void FreeMessage(NodeNetMessage& msg);

  bool test(StatusCallback callback = nullptr);

  void HandleInterrupt();
  uint8_t GetNodeAddress() const { return node_addr_; }

private:
  uint32_t base_;
  uint8_t node_addr_ = 0;
  uint32_t uart_baud_ = 0;
  MessageCallback broadcast_callback_ = nullptr;
  MessageCallback message_callback_ = nullptr;

  static uint32_t ComputeUartDivisor(uint32_t baudrate);

  static void FormatHex32(uint32_t value, char* out);

  uint32_t Read(uint32_t offset) const;

  void Write(uint32_t offset, uint32_t value) const;
};

#endif  // NODENET_H