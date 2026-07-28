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
 *   nodenet0_init(0x01, NODENET_PRIORITY_NORMAL, 200);  // Initialize as node 0x01
 *   nodenet0_send(0x02, "Hello", 5);               // Send to node 0x02
 *   if (nodenet0_has_message()) {
 *     NodeNetMessage msg = nodenet0_read();         // Read incoming message
 *     process(msg.src_addr, msg.data, msg.len);    // Process message
 *     nodenet0_free_message(msg);                  // Deallocate
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
 * 1. Inspect NODENET_STATUS for RX/TX/error bits
 * 2. Inspect NODENET_RX_HDR for [src, valid, len]
 * 3. Monitor wire protocol on H16 (RX), H17 (TX)
 * 4. Inject known frames and verify CRC/error flags
 */

#ifndef NODENET_H
#define NODENET_H

#include <cstdint>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
// Hardware Registers (Wishbone B.4 Slave at 0x10006000)
// ═══════════════════════════════════════════════════════════════════════════

#define NODENET_BASE      0x10006000u

// Mailbox registers
#define NODENET_TX_CMD    (NODENET_BASE + 0x00u)   // [dst(31:24) | len(15:0)]
#define NODENET_TX_DATA   (NODENET_BASE + 0x04u)   // write one payload byte per access
#define NODENET_RX_HDR    (NODENET_BASE + 0x08u)   // [src(31:24) | rx_valid(16) | len(15:0)]
#define NODENET_RX_DATA   (NODENET_BASE + 0x0Cu)   // read one payload byte per access
#define NODENET_CONFIG    (NODENET_BASE + 0x10u)   // [hb_interval(31:10) | prio(9:8) | addr(7:0)]
#define NODENET_CONTROL   (NODENET_BASE + 0x14u)   // bit0=trigger_tx bit1=clear_rx bit2=queue_heartbeat
#define NODENET_STATUS    (NODENET_BASE + 0x18u)
#define NODENET_LED_CFG   (NODENET_BASE + 0x1Cu)   // activity blink duration in milliseconds

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

// ═══════════════════════════════════════════════════════════════════════════
// Protocol Constants
// ═══════════════════════════════════════════════════════════════════════════

#define NODENET_BROADCAST 0x00  // Address 0 means broadcast to all nodes

// Message structure returned by nodenet0_read()
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

static inline uint32_t nodenet_reg_read(uint32_t addr) {
  return *(volatile uint32_t*)addr;
}

static inline void nodenet_reg_write(uint32_t addr, uint32_t value) {
  *(volatile uint32_t*)addr = value;
}

static inline uint32_t nodenet_status(void) {
  return nodenet_reg_read(NODENET_STATUS);
}

static inline bool nodenet_tx_mailbox_ready(void) {
  uint32_t status = nodenet_status();
  return (status & (NODENET_STATUS_TX_STAGE_VALID | NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE)) == 0;
}

static inline bool nodenet_tx_has_space(uint16_t msg_len) {
  return msg_len <= NODENET_MAX_PAYLOAD_SIZE && nodenet_tx_mailbox_ready();
}

static inline bool nodenet_rx_has_data(void) {
  return (nodenet_reg_read(NODENET_RX_HDR) & (1u << 16)) != 0;
}

static inline void nodenet_tx_write(uint8_t dst, const uint8_t* data, uint16_t len) {
  nodenet_reg_write(NODENET_TX_CMD, ((uint32_t)dst << 24) | (uint32_t)len);
  for (uint16_t index = 0; index < len; ++index) {
    nodenet_reg_write(NODENET_TX_DATA, data[index]);
  }
  nodenet_reg_write(NODENET_CONTROL, 0x1u);
}

static inline NodeNetMessage nodenet_rx_read(void) {
  uint32_t header = nodenet_reg_read(NODENET_RX_HDR);
  NodeNetMessage msg;

  msg.src_addr = (uint8_t)(header >> 24);
  msg.len = (uint16_t)(header & 0xFFFFu);
  msg.data = new uint8_t[msg.len ? msg.len : 1];

  for (uint16_t index = 0; index < msg.len; ++index) {
    msg.data[index] = (uint8_t)nodenet_reg_read(NODENET_RX_DATA);
  }

  if (msg.len == 0) {
    msg.data[0] = 0;
  }

  return msg;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API Functions (User-Facing)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Initialize NodeNet485 node
 * 
 * Must be called once at startup before using any other functions.
 * This configures the node address, priority, and clears FIFO pointers.
 * 
 * @param addr Node address on the bus (1-255; 0 is reserved for broadcast)
 * @param priority Message transmission priority (LOW, NORMAL, or HIGH)
 * @param led_blink_ms Activity LED pulse width in milliseconds (TX/RX)
 * 
 * Example:
 *   nodenet0_init(0x01, NODENET_PRIORITY_NORMAL, 200);  // This node is 0x01
 */
static inline void nodenet0_init(uint8_t addr, NodeNetPriority priority, uint32_t led_blink_ms = 100u) {
  nodenet_reg_write(NODENET_CONFIG, ((uint32_t)priority << 8) | (uint32_t)addr);
  nodenet_reg_write(NODENET_LED_CFG, led_blink_ms);
  nodenet_reg_write(NODENET_CONTROL, 0x2u);
}

/**
 * Send a message to another node
 * 
 * Queues a message for transmission to a specific destination node.
 * If destination is NODENET_BROADCAST (0), message goes to all nodes.
 * 
 * This function busy-waits if TX FIFO is full. Use with caution in
 * interrupt-driven code!
 * 
 * @param dst Destination node address (1-255, or 0 for broadcast)
 * @param data Pointer to payload bytes
 * @param len Number of bytes to send
 * 
 * Example:
 *   uint8_t greeting[] = "Hello";
 *   nodenet0_send(0x02, greeting, 5);
 * 
 * TODO: Add timeout parameter to avoid hanging on full FIFO
 */
static inline void nodenet0_send(uint8_t dst, const uint8_t* data, uint16_t len) {
  if (len > NODENET_MAX_PAYLOAD_SIZE) {
    len = NODENET_MAX_PAYLOAD_SIZE;
  }

  while (!nodenet_tx_has_space(len)) {
  }

  nodenet_tx_write(dst, data, len);
}

/**
 * Send a C-string message to another node
 * 
 * Convenience overload that automatically calculates string length.
 * 
 * @param dst Destination node address
 * @param str Null-terminated C-string to send
 * 
 * Example:
 *   nodenet0_send(0x02, "Temperature: 25C");
 */
static inline void nodenet0_send(uint8_t dst, const char* str) {
  nodenet0_send(dst, (const uint8_t*)str, strlen(str));
}

/**
 * Broadcast a message to all nodes
 * 
 * Sends message with destination = 0x00 (broadcast).
 * All nodes with different addresses will receive it.
 * 
 * @param data Pointer to payload bytes
 * @param len Number of bytes to send
 */
static inline void nodenet0_broadcast(const uint8_t* data, uint16_t len) {
  nodenet0_send(NODENET_BROADCAST, data, len);
}

/**
 * Broadcast a C-string to all nodes
 * 
 * @param str Null-terminated string to broadcast
 * 
 * Example:
 *   nodenet0_broadcast("SYSTEM_RESET");
 */
static inline void nodenet0_broadcast(const char* str) {
  nodenet0_send(NODENET_BROADCAST, str);
}

/**
 * Check if any messages are pending
 * 
 * @return true if at least one message in RX FIFO
 * 
 * Example:
 *   if (nodenet0_has_message()) {
 *     NodeNetMessage msg = nodenet0_read();
 *     ...
 *   }
 */
static inline bool nodenet0_has_message() {
  return nodenet_rx_has_data();
}

/**
 * Get approximate count of pending messages
 * 
 * NOTE: This is a simple 0/1 indicator, not an exact count!
 * We return 1 if FIFO has ANY data, 0 if empty.
 * (Exact counting requires knowing message boundaries)
 * 
 * @return 0 if no messages, 1 if messages available
 */
static inline uint8_t nodenet0_message_count() {
  return nodenet_rx_has_data() ? 1 : 0;
}

/**
 * Read and return the next pending message
 * 
 * Removes the message from RX FIFO and returns its contents.
 * 
 * WARNING: Allocates memory for msg.data! You MUST call
 * nodenet0_free_message(msg) after processing to avoid leaks.
 * 
 * @return NodeNetMessage with src_addr, len, and data buffer
 * 
 * Example:
 *   NodeNetMessage msg = nodenet0_read();
 *   printf("From node 0x%02X: %d bytes\n", msg.src_addr, msg.len);
 *   nodenet0_free_message(msg);  // Don't forget!
 */
static inline NodeNetMessage nodenet0_read() {
  return nodenet_rx_read();
}

/**
 * Deallocate a message buffer
 * 
 * Frees the dynamically allocated data buffer and clears the struct.
 * MUST be called after reading each message to prevent memory leaks!
 * 
 * @param msg Message to deallocate
 * 
 * Example:
 *   NodeNetMessage msg = nodenet0_read();
 *   process(msg);
 *   nodenet0_free_message(msg);  // Always cleanup
 */
static inline void nodenet0_free_message(NodeNetMessage& msg) {
  delete[] msg.data;
  msg.data = nullptr;
  msg.len = 0;
}

#endif  // NODENET_H
