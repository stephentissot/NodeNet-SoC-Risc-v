/**
 * @file nodenet_defines.vh
 * @brief NodeNet485 Protocol Definitions (as Verilog macros)
 * 
 * Include this file in modules that need NodeNet485 constants.
 * Usage: `include "nodenet_defines.vh"
 */

// Protocol control characters (HDLC-like framing)
`define NODENET_SOH 8'h01  // Start of Header
`define NODENET_STX 8'h02  // Start of Text
`define NODENET_ETX 8'h03  // End of Text
`define NODENET_EOT 8'h04  // End of Transmission
`define NODENET_LF  8'h0A  // Line Feed

// Protocol constraints
`define NODENET_MAX_PAYLOAD_SIZE 2048
`define NODENET_MAX_ENCODED_SIZE (2 * `NODENET_MAX_PAYLOAD_SIZE + 14)
`define NODENET_RECEIVE_TIMEOUT_CYCLES 25000000  // ~1 second @ 25 MHz

// Timing parameters (in cycles @ 25 MHz)
`define NODENET_BROADCAST_DELAY_CYCLES 1250000    // 50ms per NodeNet address unit
`define NODENET_LINE_READY_CYCLES 250000          // 10ms before unicast
`define NODENET_HEARTBEAT_DEFAULT_CYCLES 250000000 // 10 seconds

// Anti-collision timing (per address unit)
`define NODENET_UNICAST_DELAY_PER_ADDR 50000     // 2ms @ 25 MHz
`define NODENET_BROADCAST_DELAY_PER_ADDR 1250000  // 50ms @ 25 MHz
