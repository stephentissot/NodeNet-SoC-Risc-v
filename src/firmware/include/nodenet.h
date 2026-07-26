/**
 * @file nodenet.h
 * @brief NodeNet485 Wishbone Driver with SDRAM Ring Buffers
 * 
 * Uses 1 MB SDRAM (512 KB TX + 512 KB RX) for message queues.
 * Firmware manages ring buffers via Wishbone pointers.
 * 
 * SDRAM Layout:
 *   0x20000000 - 0x2007FFFF : TX ring buffer (512 KB)
 *   0x20080000 - 0x200FFFFF : RX ring buffer (512 KB)
 * 
 * Usage:
 *   nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
 *   nodenet0_send(0x02, "Hello", 5);
 *   if (nodenet0_has_message()) {
 *     msg = nodenet0_read();
 *     process_message(msg);
 *   }
 */

#ifndef NODENET_H
#define NODENET_H

#include <cstdint>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// Register Map (Wishbone addresses)
// ─────────────────────────────────────────────────────────────

#define NODENET_BASE      0x10006000
#define NODENET_TX_WPTR   (NODENET_BASE + 0x00)   // TX write pointer (fw writes)
#define NODENET_TX_RPTR   (NODENET_BASE + 0x04)   // TX read pointer (hw reads)
#define NODENET_RX_WPTR   (NODENET_BASE + 0x08)   // RX write pointer (hw writes)
#define NODENET_RX_RPTR   (NODENET_BASE + 0x0C)   // RX read pointer (fw reads)
#define NODENET_CONFIG    (NODENET_BASE + 0x10)   // [addr(8), prio(2), hb_interval(22)]
#define NODENET_CONTROL   (NODENET_BASE + 0x14)   // [reserved(31), trigger_tx(1)]
#define NODENET_STATUS    (NODENET_BASE + 0x18)   // Status bits

// Status register bits
#define NODENET_STATUS_RX_VALID(x)  ((x >> 23) & 1)
#define NODENET_STATUS_TX_ACTIVE(x) ((x >> 22) & 1)

// SDRAM FIFO Configuration
#define SDRAM_BASE        0x20000000
#define NODENET_TX_FIFO   (SDRAM_BASE + 0x00000)    // 512 KB
#define NODENET_RX_FIFO   (SDRAM_BASE + 0x80000)    // 512 KB
#define NODENET_FIFO_SIZE 0x80000                   // 512 KB each

// Broadcast address
#define NODENET_BROADCAST 0x00

// Message structure
struct NodeNetMessage {
  uint8_t src_addr;
  uint16_t len;
  uint8_t* data;
};

// Priority levels
enum NodeNetPriority {
  NODENET_PRIORITY_LOW = 0,
  NODENET_PRIORITY_NORMAL = 1,
  NODENET_PRIORITY_HIGH = 2
};

// ─────────────────────────────────────────────────────────────
// SDRAM Ring Buffer Helpers
// ─────────────────────────────────────────────────────────────

// Wrap pointer to FIFO size
static inline uint32_t nodenet_wrap_ptr(uint32_t ptr) {
  while (ptr >= NODENET_FIFO_SIZE) {
    ptr -= NODENET_FIFO_SIZE;
  }
  return ptr;
}

// TX FIFO: write message from firmware
// Frame format: [dst(1)|len_hi(1)|len_lo(1)|payload(N)]
static inline void nodenet_tx_write(uint8_t dst, const uint8_t* data, uint16_t len) {
  volatile uint32_t* tx_wptr_reg = (volatile uint32_t*)NODENET_TX_WPTR;
  volatile uint8_t* tx_fifo = (volatile uint8_t*)NODENET_TX_FIFO;
  
  uint32_t wptr = *tx_wptr_reg;
  
  // Write header: destination
  tx_fifo[wptr] = dst;
  wptr = nodenet_wrap_ptr(wptr + 1);
  
  // Write length (big-endian)
  tx_fifo[wptr] = (len >> 8) & 0xFF;
  wptr = nodenet_wrap_ptr(wptr + 1);
  
  tx_fifo[wptr] = len & 0xFF;
  wptr = nodenet_wrap_ptr(wptr + 1);
  
  // Copy payload
  for (uint16_t i = 0; i < len; i++) {
    tx_fifo[wptr] = data[i];
    wptr = nodenet_wrap_ptr(wptr + 1);
  }
  
  // Update write pointer (hardware will read and transmit)
  *tx_wptr_reg = wptr;
}

// RX FIFO: read message for firmware
// Frame format: [src(1)|len_hi(1)|len_lo(1)|payload(N)]
static inline NodeNetMessage nodenet_rx_read() {
  volatile uint32_t* rx_rptr_reg = (volatile uint32_t*)NODENET_RX_RPTR;
  volatile uint8_t* rx_fifo = (volatile uint8_t*)NODENET_RX_FIFO;
  
  NodeNetMessage msg;
  uint32_t rptr = *rx_rptr_reg;
  
  // Read header: source address
  msg.src_addr = rx_fifo[rptr];
  rptr = nodenet_wrap_ptr(rptr + 1);
  
  // Read length (big-endian)
  uint16_t len_hi = rx_fifo[rptr];
  rptr = nodenet_wrap_ptr(rptr + 1);
  
  uint16_t len_lo = rx_fifo[rptr];
  rptr = nodenet_wrap_ptr(rptr + 1);
  
  msg.len = (len_hi << 8) | len_lo;
  
  // Allocate and copy payload
  msg.data = new uint8_t[msg.len];
  for (uint16_t i = 0; i < msg.len; i++) {
    msg.data[i] = rx_fifo[rptr];
    rptr = nodenet_wrap_ptr(rptr + 1);
  }
  
  // Update read pointer (hardware can now write to this space)
  *rx_rptr_reg = rptr;
  
  return msg;
}

// Check if RX FIFO has data
static inline bool nodenet_rx_has_data() {
  volatile uint32_t* rx_rptr_reg = (volatile uint32_t*)NODENET_RX_RPTR;
  volatile uint32_t* rx_wptr_reg = (volatile uint32_t*)NODENET_RX_WPTR;
  return *rx_rptr_reg != *rx_wptr_reg;
}

// Check if TX FIFO has space for message
// (3 bytes header + payload)
static inline bool nodenet_tx_has_space(uint16_t msg_len) {
  volatile uint32_t* tx_wptr_reg = (volatile uint32_t*)NODENET_TX_WPTR;
  volatile uint32_t* tx_rptr_reg = (volatile uint32_t*)NODENET_TX_RPTR;
  
  uint32_t wptr = *tx_wptr_reg;
  uint32_t rptr = *tx_rptr_reg;
  
  // Space needed: 3 bytes header + payload
  uint32_t needed = 3 + msg_len;
  
  // Calculate available space in ring buffer
  uint32_t avail;
  if (rptr <= wptr) {
    // Normal case: write pointer hasn't wrapped past read pointer
    avail = NODENET_FIFO_SIZE - (wptr - rptr);
  } else {
    // Wrapped case: read pointer ahead of write pointer
    avail = rptr - wptr;
  }
  
  return avail >= (needed + 1);  // +1 for safety margin
}

// ─────────────────────────────────────────────────────────────
// High-level API
// ─────────────────────────────────────────────────────────────

static inline void nodenet0_init(uint8_t addr, NodeNetPriority priority) {
  volatile uint32_t* cfg = (volatile uint32_t*)NODENET_CONFIG;
  volatile uint32_t* tx_wptr = (volatile uint32_t*)NODENET_TX_WPTR;
  volatile uint32_t* tx_rptr = (volatile uint32_t*)NODENET_TX_RPTR;
  volatile uint32_t* rx_rptr = (volatile uint32_t*)NODENET_RX_RPTR;
  
  // Configure node address and priority only
  // (heartbeat_interval left at hardware default: ~10 seconds)
  *cfg = (addr << 0) | (priority << 8);
  
  // Clear FIFO pointers
  *tx_wptr = 0;
  *tx_rptr = 0;
  *rx_rptr = 0;
}

static inline void nodenet0_send(uint8_t dst, const uint8_t* data, uint16_t len) {
  // Wait for TX FIFO to have space
  // TODO: implement timeout in production
  while (!nodenet_tx_has_space(len)) {
    // Busy-wait (no OS, bare-metal)
  }
  nodenet_tx_write(dst, data, len);
}

static inline void nodenet0_send(uint8_t dst, const char* str) {
  nodenet0_send(dst, (const uint8_t*)str, strlen(str));
}

static inline void nodenet0_broadcast(const uint8_t* data, uint16_t len) {
  nodenet0_send(NODENET_BROADCAST, data, len);
}

static inline void nodenet0_broadcast(const char* str) {
  nodenet0_send(NODENET_BROADCAST, str);
}

static inline bool nodenet0_has_message() {
  return nodenet_rx_has_data();
}

static inline uint8_t nodenet0_message_count() {
  // Simple check: 0 or 1 (doesn't count actual messages)
  return nodenet_rx_has_data() ? 1 : 0;
}

static inline NodeNetMessage nodenet0_read() {
  return nodenet_rx_read();
}

static inline void nodenet0_free_message(NodeNetMessage& msg) {
  delete[] msg.data;
  msg.data = nullptr;
  msg.len = 0;
}

#endif  // NODENET_H
