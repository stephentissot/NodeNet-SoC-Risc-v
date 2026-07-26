/**
 * @file nodenet_types.sv
 * @brief NodeNet485 Protocol Type Definitions
 * 
 * Defines protocol constants and the message structure for the NodeNet485
 * communication protocol. This follows the ESP32 implementation.
 */

package nodenet_types;

  // Protocol control characters (HDLC-like framing)
  localparam SOH = 8'h01;  // Start of Header
  localparam STX = 8'h02;  // Start of Text
  localparam ETX = 8'h03;  // End of Text
  localparam EOT = 8'h04;  // End of Transmission
  localparam LF  = 8'h0A;  // Line Feed

  // Protocol constraints
  localparam MAX_PAYLOAD_SIZE = 2048;
  localparam MAX_ENCODED_SIZE = 2 * MAX_PAYLOAD_SIZE + 14;  // 2x for nibble encoding + framing
  localparam RECEIVE_TIMEOUT_CYCLES = 25000000;  // ~1 second @ 25 MHz

  // Timing parameters (in cycles @ 25 MHz)
  localparam BROADCAST_DELAY_CYCLES = 1250000;    // 50ms per NodeNet address unit
  localparam LINE_READY_CYCLES = 250000;          // 10ms before unicast
  localparam HEARTBEAT_DEFAULT_CYCLES = 250000000; // 10 seconds

  // Anti-collision timing
  // nextTransmitAllowed = now + addr * 2ms (unicast) or addr * 50ms (broadcast)
  localparam UNICAST_DELAY_PER_ADDR = 50000;     // 2ms @ 25 MHz
  localparam BROADCAST_DELAY_PER_ADDR = 1250000;  // 50ms @ 25 MHz

endpackage
