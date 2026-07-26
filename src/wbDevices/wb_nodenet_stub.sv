/**
 * @file wb_nodenet.sv
 * @brief NodeNet485 Wishbone Slave Module (STUB VERSION)
 * 
 * Main integration point for NodeNet485 protocol over RS-485 UART.
 * Implements Wishbone B.4 interface.
 * 
 * Register Map (offsets from base address):
 *   0x00: TX_CMD (write)     - Initiate message transmission
 *   0x04: TX_DATA (write)    - Queue bytes for transmission
 *   0x08: RX_DATA (read)     - Read received message data
 *   0x0C: STATUS (read)      - Status flags and message counts
 *   0x10: CONFIG (read/write)- Heartbeat interval, node address, priority
 */

`include "src/wbDevices/nodenet_defines.vh"

module wb_nodenet #(
  parameter CLOCK_RATE = 25_000_000,
  parameter FIFO_DEPTH = 8,
  parameter MAX_PAYLOAD = 2048
) (
  // Wishbone B.4 interface
  input wire clk_i,
  input wire rst_i,
  
  input wire [31:0] adr_i,
  input wire [31:0] dat_i,
  output wire [31:0] dat_o,
  input wire we_i,
  input wire stb_i,
  input wire cyc_i,
  output wire ack_o,
  
  // UART interface
  input wire uart_rx_i,
  output wire uart_tx_o,
  output wire uart_de_o,  // RS485 driver enable (DE/RE)
  
  // Interrupt (optional, for future use)
  output wire irq_o
);

  wire clk = clk_i;
  wire rst_n = ~rst_i;
  
  // ─────────────────────────────────────────────────────────────
  // Wishbone Interface
  // ─────────────────────────────────────────────────────────────
  
  wire write_en = cyc_i && stb_i && we_i;
  wire read_en = cyc_i && stb_i && ~we_i;
  wire [3:0] addr = adr_i[5:2];  // Byte offset to register index
  
  reg [31:0] dat_o_reg;
  assign dat_o = dat_o_reg;
  
  reg ack_delayed;
  assign ack_o = ack_delayed;
  
  // Configuration registers
  reg [7:0] node_addr;
  reg [31:0] heartbeat_interval;
  reg [1:0] prio;
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      ack_delayed <= 1'b0;
      node_addr <= 8'h01;  // Default address
      heartbeat_interval <= 32'd250_000_000;  // 10 seconds @ 25MHz
      prio <= 2'b01;  // NORMAL priority
    end
    else begin
      ack_delayed <= cyc_i && stb_i && !ack_delayed;
      
      // Write transactions
      if (write_en) begin
        case (addr)
          4'h0: ; // TX_CMD - handled by separate logic
          4'h1: ; // TX_DATA - handled by separate logic
          4'h4: begin
            // CONFIG register
            node_addr <= dat_i[7:0];
            prio <= dat_i[9:8];
            heartbeat_interval <= dat_i[31:10] << 10;  // Scale up
          end
        endcase
      end
      
      // Read transactions
      if (read_en) begin
        case (addr)
          4'h2: dat_o_reg <= {16'b0, 8'b0, 1'b0, 1'b0, 5'b0};  // STATUS
          4'h4: dat_o_reg <= {node_addr, prio, 22'b0};  // CONFIG
          default: dat_o_reg <= 32'b0;
        endcase
      end
    end
  end
  
  // ─────────────────────────────────────────────────────────────
  // UART Loopback (echo for testing)
  // ─────────────────────────────────────────────────────────────
  
  // Simple echo: RX → TX
  reg uart_rx_sync1, uart_rx_sync2;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      uart_rx_sync1 <= 1'b1;
      uart_rx_sync2 <= 1'b1;
    end
    else begin
      uart_rx_sync1 <= uart_rx_i;
      uart_rx_sync2 <= uart_rx_sync1;
    end
  end
  
  // Simple TX pass-through for now
  assign uart_tx_o = uart_rx_sync2;
  assign uart_de_o = 1'b1;  // Always enable RS485 driver for testing
  assign irq_o = 1'b0;

endmodule
