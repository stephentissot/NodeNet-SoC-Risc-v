/**
 * @file wb_nodenet.sv
 * @brief NodeNet485 Wishbone B.4 Slave with SDRAM Ring Buffers
 * 
 * High-level Overview:
 * ────────────────────
 * This module implements a multi-node message passing protocol over RS-485 at 1 Mb/s.
 * Instead of on-chip BRAM FIFOs (limited to ~32-50 KB), we use SDRAM ring buffers
 * (512 KB TX + 512 KB RX) managed by firmware pointers.
 * 
 * Architecture:
 * ─────────────
 *   Wishbone Bus
 *       ↓ (register reads/writes)
 *   Pointer Registers (TX_WPTR, TX_RPTR, RX_WPTR, RX_RPTR)
 *       ↓
 *   SDRAM Ring Buffers
 *       ↓
 *   UART Interface (1 Mb/s)
 *       ↓
 *   RS-485 Transceiver
 * 
 * Ring Buffer Operations:
 * ──────────────────────
 *   TX Path (Firmware → Hardware → RS-485):
 *     1. Firmware writes message to SDRAM at TX_WPTR
 *     2. Firmware updates TX_WPTR register
 *     3. Hardware detects TX_WPTR != TX_READ_PTR
 *     4. Hardware reads from SDRAM at TX_READ_PTR
 *     5. Hardware transmits via UART
 *     6. Hardware updates TX_READ_PTR (message consumed)
 * 
 *   RX Path (RS-485 → Hardware → Firmware):
 *     1. Hardware receives bytes from UART
 *     2. Hardware writes to SDRAM at RX_WPTR
 *     3. Hardware updates RX_WPTR
 *     4. Firmware checks RX_RPTR != RX_WPTR
 *     5. Firmware reads from SDRAM at RX_RPTR
 *     6. Firmware updates RX_RPTR (message consumed)
 * 
 * Current Status:
 * ───────────────
 *   - Pointer registers fully implemented
 *   - Simple loopback mode active (RX→TX echo for testing)
 *   - Encoder/Decoder FSMs available but not yet wired
 *   - Ready for full protocol implementation
 * 
 * @param CLOCK_RATE     System clock in Hz (25 MHz default)
 * @param SDRAM_TX_BASE  TX FIFO start address (0x20000000 default)
 * @param SDRAM_RX_BASE  RX FIFO start address (0x20080000 default)
 * @param SDRAM_FIFO_SIZE Size of each FIFO (512 KB each)
 */

module wb_nodenet #(
  parameter [31:0] CLOCK_RATE = 25_000_000,
  parameter [31:0] SDRAM_TX_BASE = 32'h20000000,
  parameter [31:0] SDRAM_RX_BASE = 32'h20080000,
  parameter [31:0] SDRAM_FIFO_SIZE = 32'h80000
) (
  // ════════════════════════════════════════════════════════════════════════
  // Wishbone B.4 Interface
  // ════════════════════════════════════════════════════════════════════════
  
  input  wire clk_i,                // System clock (25 MHz)
  input  wire rst_i,                // Reset (active high)
  
  // Address bus (32-bit, split into 5-bit register offset)
  input  wire [31:0] adr_i,
  
  // Data: From master to slave (write operations)
  input  wire [31:0] dat_i,
  
  // Data: From slave to master (read operations)
  output reg  [31:0] dat_o,
  
  // Write enable: 1 = write, 0 = read
  input  wire we_i,
  
  // Byte select (typically all 1's for 32-bit access)
  input  wire sel_i,
  
  // Cycle signal (transaction in progress)
  input  wire cyc_i,
  
  // Strobe signal (this cycle is selected)
  input  wire stb_i,
  
  // Acknowledge (slave ready with data)
  output wire ack_o,
  
  // ════════════════════════════════════════════════════════════════════════
  // RS-485 UART Interface
  // ════════════════════════════════════════════════════════════════════════
  
  input  wire uart_rx_i,            // Receive from RS-485 transceiver
  output wire uart_tx_o             // Transmit to RS-485 transceiver
);

  // ════════════════════════════════════════════════════════════════════════
  // Configuration Registers (backed by Wishbone)
  // ════════════════════════════════════════════════════════════════════════
  
  // Node address (0x01 - 0xFF, 0x00 is broadcast)
  reg [7:0]  node_addr;
  
  // Heartbeat interval in clock cycles
  // Default: 250_000_000 cycles @ 25 MHz = 10 seconds
  reg [31:0] heartbeat_interval;
  
  // Message priority (LOW=0, NORMAL=1, HIGH=2)
  reg [1:0]  prio;
  
  // ════════════════════════════════════════════════════════════════════════
  // Ring Buffer Pointer Registers (User-Visible via Wishbone)
  // ════════════════════════════════════════════════════════════════════════
  
  // TX FIFO: Firmware writes pointers, hardware reads
  reg [31:0] tx_write_ptr;          // Firmware advances after writing message
  reg [31:0] tx_read_ptr;           // Hardware advances after transmitting
  
  // RX FIFO: Hardware writes pointers, firmware reads
  reg [31:0] rx_write_ptr;          // Hardware advances after receiving message
  reg [31:0] rx_read_ptr;           // Firmware advances after reading message
  
  // ════════════════════════════════════════════════════════════════════════
  // Status Flags
  // ════════════════════════════════════════════════════════════════════════
  
  reg tx_active;                    // TX in progress
  reg rx_valid;                     // RX message available
  
  // ════════════════════════════════════════════════════════════════════════
  // Initialization
  // ════════════════════════════════════════════════════════════════════════
  
  initial begin
    node_addr <= 8'h01;
    heartbeat_interval <= 32'd250_000_000;
    prio <= 2'b01;
    tx_write_ptr <= 32'h0;
    tx_read_ptr <= 32'h0;
    rx_write_ptr <= 32'h0;
    rx_read_ptr <= 32'h0;
    tx_active <= 1'b0;
    rx_valid <= 1'b0;
    dat_o <= 32'h0;
  end
  
  // ════════════════════════════════════════════════════════════════════════
  // Wishbone Address Decoding
  // ════════════════════════════════════════════════════════════════════════
  
  // Extract 5-bit register offset from address bus
  // Allows 32 registers (0x00-0x1F)
  wire [4:0] addr = adr_i[4:0];
  
  // Transaction valid: cycle && strobe
  wire wb_valid = cyc_i && stb_i;
  
  // ════════════════════════════════════════════════════════════════════════
  // Loopback Mode (Current Implementation)
  // ════════════════════════════════════════════════════════════════════════
  
  // For now: RX data echoes to TX immediately (loopback for testing)
  // This allows basic hardware validation without protocol encoder/decoder
  // TODO: Replace with full encoder/decoder FSM when available
  assign uart_tx_o = uart_rx_i;
  
  // ════════════════════════════════════════════════════════════════════════
  // Wishbone Acknowledge (Always ready - no wait states)
  // ════════════════════════════════════════════════════════════════════════
  
  assign ack_o = wb_valid;
  
  // ════════════════════════════════════════════════════════════════════════
  // Register Access Logic (Read/Write)
  // ════════════════════════════════════════════════════════════════════════
  
  always @(posedge clk_i) begin
    if (rst_i) begin
      // Reset all registers to default values
      node_addr <= 8'h01;
      heartbeat_interval <= 32'd250_000_000;
      prio <= 2'b01;
      tx_write_ptr <= 32'h0;
      tx_read_ptr <= 32'h0;
      rx_write_ptr <= 32'h0;
      rx_read_ptr <= 32'h0;
      tx_active <= 1'b0;
      rx_valid <= 1'b0;
      dat_o <= 32'h0;
    end else if (wb_valid) begin
      if (we_i) begin
        // ──────────────────────────────────────────────────────────────────
        // WRITE Operations (Firmware → Hardware)
        // ──────────────────────────────────────────────────────────────────
        case (addr)
          5'h0: tx_write_ptr <= dat_i;    // TX_WRITE_PTR: Firmware updates after writing
          5'h1: tx_read_ptr <= dat_i;     // TX_READ_PTR: Firmware clears after TX complete
          5'h2: rx_write_ptr <= dat_i;    // RX_WRITE_PTR: Usually not written by firmware
          5'h3: rx_read_ptr <= dat_i;     // RX_READ_PTR: Firmware advances after reading
          5'h4: begin
            // CONFIG: [node_addr(8), prio(2), hb_interval(22)]
            // Setting node address and priority
            node_addr <= dat_i[7:0];
            prio <= dat_i[9:8];
            heartbeat_interval <= dat_i[31:10] << 10;
          end
          5'h5: begin
            // CONTROL: [reserved(31), trigger_tx(1)]
            // Writing 1 triggers immediate transmission (for future use)
            if (dat_i[0]) tx_active <= 1'b1;
          end
        endcase
      end else begin
        // ──────────────────────────────────────────────────────────────────
        // READ Operations (Hardware → Firmware)
        // ──────────────────────────────────────────────────────────────────
        case (addr)
          5'h0: dat_o <= tx_write_ptr;              // TX_WRITE_PTR: Read current position
          5'h1: dat_o <= tx_read_ptr;               // TX_READ_PTR: Read HW consumption
          5'h2: dat_o <= rx_write_ptr;              // RX_WRITE_PTR: Read HW production
          5'h3: dat_o <= rx_read_ptr;               // RX_READ_PTR: Read current consumption
          5'h4: dat_o <= {heartbeat_interval[21:0], prio, node_addr};  // CONFIG
          5'h5: begin
            // STATUS: [reserved(22), rx_valid(1), tx_active(1), tx_avail(4), rx_avail(4)]
            // Shows FIFO occupancy indicators
            dat_o <= {22'h0, 
                      rx_valid,                                    // Bit 23: RX has data
                      tx_active,                                   // Bit 22: TX in progress
                      (tx_write_ptr != tx_read_ptr) ? 4'hF : 4'h0, // Bits 21-18: TX FIFO non-empty
                      (rx_write_ptr != rx_read_ptr) ? 4'hF : 4'h0};// Bits 17-14: RX FIFO non-empty
          end
          default: dat_o <= 32'h0;
        endcase
      end
    end
  end

endmodule

