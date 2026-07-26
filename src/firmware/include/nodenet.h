/**
 * @file nodenet.h
 * @brief NodeNet485 Wishbone Driver with SDRAM Ring Buffers
 * 
 * Overview
 * ════════
 * This is the firmware API for the NodeNet485 multi-node RS-485 protocol.
 * Messages are queued in SDRAM ring buffers (512 KB each direction) managed
 * by firmware pointer arithmetic.
 * 
 * Quick Start
 * ═══════════
 *   nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);  // Initialize as node 0x01
 *   nodenet0_send(0x02, "Hello", 5);               // Send to node 0x02
 *   if (nodenet0_has_message()) {
 *     NodeNetMessage msg = nodenet0_read();         // Read incoming message
 *     process(msg.src_addr, msg.data, msg.len);    // Process message
 *     nodenet0_free_message(msg);                  // Deallocate
 *   }
 * 
 * Ring Buffer Mechanics
 * ═════════════════════
 * 
 * TX FIFO (Firmware writes, Hardware reads):
 *   Firmware:                          Hardware:
 *   1. Write to SDRAM[TX_WPTR]   →    4. Read from SDRAM[TX_RPTR]
 *   2. Update TX_WPTR register   →    5. Transmit via UART
 *   3. Check TX_RPTR changed              6. Update TX_RPTR register
 * 
 * RX FIFO (Hardware writes, Firmware reads):
 *   Hardware:                          Firmware:
 *   1. Receive from UART         →    3. Read from SDRAM[RX_RPTR]
 *   2. Write to SDRAM[RX_WPTR]   →    4. Update RX_RPTR register
 *         Update RX_WPTR register
 * 
 * Frame Format in Ring Buffer
 * ════════════════════════════
 * Each message in SDRAM is stored as:
 *   [dst(1 byte) | len_hi(1 byte) | len_lo(1 byte) | payload(N bytes)]
 * 
 * Example: Send "Hi" to node 0x02:
 *   [0x02 | 0x00 | 0x02 | 'H' | 'i']  ← 5 bytes total in ring buffer
 * 
 * Capacity
 * ════════
 * - TX FIFO: 512 KB starting at 0x20000000
 * - RX FIFO: 512 KB starting at 0x20080000
 * - Typical message: 2 KB (header + payload)
 * - Typical capacity: ~250 messages per direction
 * 
 * Pointer Wrapping
 * ════════════════
 * Ring buffers wrap around when reaching FIFO_SIZE:
 *   new_ptr = (old_ptr + size) % FIFO_SIZE
 * 
 * Example: FIFO_SIZE = 512 KB (0x80000 bytes)
 *   ptr = 0x7FFF0 (near end)
 *   msg_len = 0x20
 *   new_ptr = (0x7FFF0 + 0x20) % 0x80000 = 0x10 (wraps to start)
 * 
 * Debugging Tips
 * ══════════════
 * 1. Check TX FIFO status:  wptr = *(uint32_t*)0x10006000, rptr = *(uint32_t*)0x10006004
 * 2. Check RX FIFO status:  wptr = *(uint32_t*)0x10006008, rptr = *(uint32_t*)0x1000600C
 * 3. Read raw SDRAM:        *(uint8_t*)(0x20000000 + offset) for TX inspection
 * 4. Monitor wire protocol: Logic analyzer on H16 (RX), H17 (TX)
 * 5. Test loopback:         Currently active - send should immediately echo
 */

#ifndef NODENET_H
#define NODENET_H

#include <cstdint>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
// Hardware Registers (Wishbone B.4 Slave at 0x10006000)
// ═══════════════════════════════════════════════════════════════════════════

#define NODENET_BASE      0x10006000

// Ring buffer pointer registers (32-bit each)
#define NODENET_TX_WPTR   (NODENET_BASE + 0x00)   // TX write pointer (fw controls)
#define NODENET_TX_RPTR   (NODENET_BASE + 0x04)   // TX read pointer (hw updates)
#define NODENET_RX_WPTR   (NODENET_BASE + 0x08)   // RX write pointer (hw updates)
#define NODENET_RX_RPTR   (NODENET_BASE + 0x0C)   // RX read pointer (fw controls)

// Configuration and control registers
#define NODENET_CONFIG    (NODENET_BASE + 0x10)   // Node address + priority
#define NODENET_CONTROL   (NODENET_BASE + 0x14)   // Trigger signals
#define NODENET_STATUS    (NODENET_BASE + 0x18)   // Status flags

// ═══════════════════════════════════════════════════════════════════════════
// SDRAM Ring Buffer Configuration
// ═══════════════════════════════════════════════════════════════════════════

#define SDRAM_BASE        0x20000000

// Physical SDRAM locations (1 MB reserved for NodeNet485)
#define NODENET_TX_FIFO   (SDRAM_BASE + 0x00000)    // TX: 0x20000000 - 0x2007FFFF
#define NODENET_RX_FIFO   (SDRAM_BASE + 0x80000)    // RX: 0x20080000 - 0x200FFFFF
#define NODENET_FIFO_SIZE 0x80000                   // 512 KB each

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

// ═══════════════════════════════════════════════════════════════════════════
// Internal Ring Buffer Helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Wrap a pointer to stay within FIFO bounds [0, FIFO_SIZE)
 * 
 * This handles circular buffer overflow by using modulo arithmetic.
 * When ptr exceeds FIFO_SIZE, it wraps back to the beginning.
 * 
 * @param ptr Current pointer value
 * @return Wrapped pointer in range [0, FIFO_SIZE)
 * 
 * Example:
 *   ptr = 0x7FFF0, size = 0x20
 *   result = (0x7FFF0 + 0x20) % 0x80000 = 0x10  ← Wrapped to start
 */
static inline uint32_t nodenet_wrap_ptr(uint32_t ptr) {
  while (ptr >= NODENET_FIFO_SIZE) {
    ptr -= NODENET_FIFO_SIZE;
  }
  return ptr;
}

/**
 * Write a message to the TX ring buffer
 * 
 * This function places a message in SDRAM's TX FIFO for transmission.
 * The message is stored as: [dst(1B) | len_hi(1B) | len_lo(1B) | payload]
 * 
 * After writing, the TX_WRITE_PTR register is updated so the hardware
 * knows a new message is available. The hardware will transmit at its leisure
 * based on priority and anti-collision timing.
 * 
 * @param dst Destination node address (1-255, or 0 for broadcast)
 * @param data Payload bytes to send
 * @param len Number of payload bytes (0-65535)
 * 
 * Danger: This function will OVERWRITE memory if TX FIFO has no space!
 * Call nodenet_tx_has_space() before sending large messages.
 * 
 * Example:
 *   uint8_t msg[] = {0x41, 0x42};  // "AB"
 *   nodenet_tx_write(0x02, msg, 2);  // Send to node 0x02
 *   // In SDRAM: [0x02 | 0x00 | 0x02 | 0x41 | 0x42]
 */
static inline void nodenet_tx_write(uint8_t dst, const uint8_t* data, uint16_t len) {
  volatile uint32_t* tx_wptr_reg = (volatile uint32_t*)NODENET_TX_WPTR;
  volatile uint8_t* tx_fifo = (volatile uint8_t*)NODENET_TX_FIFO;
  
  // Read current write pointer (where to place the message)
  uint32_t wptr = *tx_wptr_reg;
  
  // Write header byte 1: Destination address
  tx_fifo[wptr] = dst;
  wptr = nodenet_wrap_ptr(wptr + 1);
  
  // Write header byte 2: Message length (high byte)
  tx_fifo[wptr] = (len >> 8) & 0xFF;
  wptr = nodenet_wrap_ptr(wptr + 1);
  
  // Write header byte 3: Message length (low byte)
  tx_fifo[wptr] = len & 0xFF;
  wptr = nodenet_wrap_ptr(wptr + 1);
  
  // Write payload bytes in order
  for (uint16_t i = 0; i < len; i++) {
    tx_fifo[wptr] = data[i];
    wptr = nodenet_wrap_ptr(wptr + 1);
  }
  
  // Update write pointer so hardware knows message is ready
  // Hardware monitors: if (TX_WRITE_PTR != TX_READ_PTR) then transmit
  *tx_wptr_reg = wptr;
}

/**
 * Read a message from the RX ring buffer
 * 
 * Retrieves the next available received message from SDRAM's RX FIFO.
 * The hardware writes messages here as they arrive from the RS-485 line.
 * 
 * The message is extracted as: [src(1B) | len_hi(1B) | len_lo(1B) | payload]
 * 
 * After reading, the RX_READ_PTR register is updated, signaling to the
 * hardware that this buffer space can be reused for future messages.
 * 
 * WARNING: The returned data pointer is dynamically allocated. You MUST call
 * nodenet0_free_message(msg) after processing to avoid memory leaks!
 * 
 * @return NodeNetMessage struct with src_addr, len, and allocated data buffer
 * 
 * Example:
 *   if (nodenet0_has_message()) {
 *     NodeNetMessage msg = nodenet0_read();
 *     printf("From 0x%02X: len=%d\n", msg.src_addr, msg.len);
 *     nodenet0_free_message(msg);  // IMPORTANT!
 *   }
 */
static inline NodeNetMessage nodenet_rx_read() {
  volatile uint32_t* rx_rptr_reg = (volatile uint32_t*)NODENET_RX_RPTR;
  volatile uint8_t* rx_fifo = (volatile uint8_t*)NODENET_RX_FIFO;
  
  NodeNetMessage msg;
  uint32_t rptr = *rx_rptr_reg;
  
  // Read header byte 1: Source address
  msg.src_addr = rx_fifo[rptr];
  rptr = nodenet_wrap_ptr(rptr + 1);
  
  // Read header byte 2: Message length (high byte)
  uint16_t len_hi = rx_fifo[rptr];
  rptr = nodenet_wrap_ptr(rptr + 1);
  
  // Read header byte 3: Message length (low byte)
  uint16_t len_lo = rx_fifo[rptr];
  rptr = nodenet_wrap_ptr(rptr + 1);
  
  // Reconstruct 16-bit length from two bytes
  msg.len = (len_hi << 8) | len_lo;
  
  // Allocate buffer for payload and copy bytes
  msg.data = new uint8_t[msg.len];
  for (uint16_t i = 0; i < msg.len; i++) {
    msg.data[i] = rx_fifo[rptr];
    rptr = nodenet_wrap_ptr(rptr + 1);
  }
  
  // Update read pointer to mark this space as consumed
  // Hardware can now overwrite this buffer space with future messages
  *rx_rptr_reg = rptr;
  
  return msg;
}

/**
 * Check if there are any pending RX messages
 * 
 * @return true if RX FIFO has data (RX_READ_PTR != RX_WRITE_PTR)
 * 
 * Use this in your main loop before calling nodenet0_read():
 *   while (1) {
 *     if (nodenet0_has_message()) {
 *       msg = nodenet0_read();
 *       ...
 *     }
 *   }
 */
static inline bool nodenet_rx_has_data() {
  volatile uint32_t* rx_rptr_reg = (volatile uint32_t*)NODENET_RX_RPTR;
  volatile uint32_t* rx_wptr_reg = (volatile uint32_t*)NODENET_RX_WPTR;
  return *rx_rptr_reg != *rx_wptr_reg;
}

/**
 * Check if there's enough space in TX FIFO for a message
 * 
 * Ring buffer available space is:
 *   - If RPTR <= WPTR: space = SIZE - (WPTR - RPTR)
 *   - If RPTR >  WPTR: space = RPTR - WPTR
 * 
 * @param msg_len Message payload length to check
 * @return true if FIFO has space for header (3 bytes) + payload
 * 
 * Use this before sending large messages to avoid overwriting data:
 *   uint8_t huge_msg[1024];
 *   if (nodenet_tx_has_space(1024)) {
 *     nodenet0_send(0x02, huge_msg, 1024);
 *   } else {
 *     // Wait for transmission or error
 *   }
 */
static inline bool nodenet_tx_has_space(uint16_t msg_len) {
  volatile uint32_t* tx_wptr_reg = (volatile uint32_t*)NODENET_TX_WPTR;
  volatile uint32_t* tx_rptr_reg = (volatile uint32_t*)NODENET_TX_RPTR;
  
  uint32_t wptr = *tx_wptr_reg;
  uint32_t rptr = *tx_rptr_reg;
  
  // Space needed: 3-byte header + payload
  uint32_t needed = 3 + msg_len;
  
  // Calculate available space in ring buffer
  uint32_t avail;
  if (rptr <= wptr) {
    // Normal case: write pointer hasn't wrapped past read pointer
    // Available = size - used
    avail = NODENET_FIFO_SIZE - (wptr - rptr);
  } else {
    // Wrapped case: read pointer ahead of write pointer
    // Available = gap between them
    avail = rptr - wptr;
  }
  
  // Return true only if we have enough space plus safety margin
  return avail >= (needed + 1);
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
 * 
 * Example:
 *   nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);  // This node is 0x01
 */
static inline void nodenet0_init(uint8_t addr, NodeNetPriority priority) {
  volatile uint32_t* cfg = (volatile uint32_t*)NODENET_CONFIG;
  volatile uint32_t* tx_wptr = (volatile uint32_t*)NODENET_TX_WPTR;
  volatile uint32_t* tx_rptr = (volatile uint32_t*)NODENET_TX_RPTR;
  volatile uint32_t* rx_rptr = (volatile uint32_t*)NODENET_RX_RPTR;
  
  // Configure node address and priority
  // Heartbeat interval left at hardware default (10 seconds @ 25MHz)
  *cfg = (addr << 0) | (priority << 8);
  
  // Clear all ring buffer pointers (both FIFOs empty)
  *tx_wptr = 0;
  *tx_rptr = 0;
  *rx_rptr = 0;
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
  // Wait for TX FIFO to have space
  // TODO: Implement timeout in production to avoid infinite wait
  while (!nodenet_tx_has_space(len)) {
    // Busy-wait (no OS, bare-metal)
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
