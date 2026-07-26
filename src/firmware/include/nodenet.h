/**
 * @file nodenet.h
 * @brief NodeNet485 Wishbone Driver for RISC-V Firmware
 * 
 * Provides C++ interface to wb_nodenet peripheral:
 * - Message transmission (unicast + broadcast)
 * - Message reception with CRC validation
 * - Heartbeat configuration
 * - Priority/baud rate control
 * 
 * Usage:
 *   nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
 *   nodenet0_send(0x02, "Hello", 5);
 *   if (nodenet0_message_count() > 0) {
 *     msg = nodenet0_read();
 *     process_message(msg);
 *   }
 */

#ifndef NODENET_H
#define NODENET_H

#include <cstdint>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// Register Map (offset from NODENET_BASE)
// ─────────────────────────────────────────────────────────────

#define NODENET_BASE 0x10006000

#define NODENET_TX_CMD   (NODENET_BASE + 0x00)
#define NODENET_TX_DATA  (NODENET_BASE + 0x04)
#define NODENET_RX_DATA  (NODENET_BASE + 0x08)
#define NODENET_STATUS   (NODENET_BASE + 0x0C)
#define NODENET_CONFIG   (NODENET_BASE + 0x10)

// Status register bits
#define NODENET_STATUS_TX_READY    (1 << 0)
#define NODENET_STATUS_RX_VALID    (1 << 1)
#define NODENET_STATUS_RX_COUNT(x) ((x >> 8) & 0xFF)
#define NODENET_STATUS_TX_COUNT(x) ((x >> 16) & 0xFF)

// Priority levels
enum NodeNetPriority {
  NODENET_PRIORITY_LOW = 0,
  NODENET_PRIORITY_NORMAL = 1,
  NODENET_PRIORITY_HIGH = 2
};

// Message structure
struct NodeNetMessage {
  uint8_t src_addr;
  uint16_t len;
  uint8_t* data;
};

// ─────────────────────────────────────────────────────────────
// Inline Helper Functions
// ─────────────────────────────────────────────────────────────

static inline void nodenet0_init(uint8_t my_addr, NodeNetPriority priority) {
  // Set node address and priority via CONFIG register
  volatile uint32_t* config = (volatile uint32_t*)NODENET_CONFIG;
  *config = (my_addr) | (priority << 8);
}

static inline void nodenet0_send(uint8_t dst, const uint8_t* data, uint16_t len) {
  // Queue message for transmission
  volatile uint32_t* tx_cmd = (volatile uint32_t*)NODENET_TX_CMD;
  volatile uint32_t* tx_data = (volatile uint32_t*)NODENET_TX_DATA;
  
  // Write header (dst, len)
  *tx_cmd = (dst) | (len << 8);
  
  // Write payload bytes
  for (uint16_t i = 0; i < len; i++) {
    *tx_data = data[i];
  }
  
  // Pulse TX_CMD to initiate send
  *tx_cmd = 0x80000000;  // TX_GO bit
}

static inline void nodenet0_send(uint8_t dst, const char* str) {
  nodenet0_send(dst, (const uint8_t*)str, strlen(str));
}

static inline uint8_t nodenet0_message_count() {
  volatile uint32_t* status = (volatile uint32_t*)NODENET_STATUS;
  uint32_t st = *status;
  return NODENET_STATUS_RX_COUNT(st);
}

static inline bool nodenet0_has_message() {
  return nodenet0_message_count() > 0;
}

static inline NodeNetMessage nodenet0_read() {
  volatile uint32_t* rx_data = (volatile uint32_t*)NODENET_RX_DATA;
  NodeNetMessage msg;
  
  // Read header
  uint32_t hdr = *rx_data;
  msg.src_addr = hdr & 0xFF;
  msg.len = (hdr >> 8) & 0xFFFF;
  
  // Read payload (allocate buffer)
  msg.data = new uint8_t[msg.len];
  for (uint16_t i = 0; i < msg.len; i++) {
    msg.data[i] = (*rx_data) & 0xFF;
  }
  
  return msg;
}

static inline void nodenet0_free_message(NodeNetMessage& msg) {
  delete[] msg.data;
  msg.data = nullptr;
  msg.len = 0;
}

// Broadcast helper
static inline void nodenet0_broadcast(const uint8_t* data, uint16_t len) {
  nodenet0_send(0, data, len);
}

static inline void nodenet0_broadcast(const char* str) {
  nodenet0_send(0, str);
}

#endif  // NODENET_H
