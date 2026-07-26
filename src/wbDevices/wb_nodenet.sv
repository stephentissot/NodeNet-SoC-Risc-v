/**
 * @file wb_nodenet.sv
 * @brief NodeNet485 Wishbone Slave - Minimal Stub
 * 
 * Provides basic register read/write for hardware integration testing.
 * Full encoder/decoder/FIFO implementation to follow.
 */

module wb_nodenet #(
  parameter CLOCK_RATE = 25_000_000,
  parameter FIFO_DEPTH = 8,
  parameter MAX_PAYLOAD = 2048
) (
  // Wishbone B.4 interface
  input  wire clk_i,
  input  wire rst_i,
  input  wire [31:0] adr_i,
  input  wire [31:0] dat_i,
  output reg  [31:0] dat_o,
  input  wire we_i,
  input  wire sel_i,
  input  wire cyc_i,
  input  wire stb_i,
  output wire ack_o,
  
  // UART/RS485 Interface
  input  wire uart_rx_i,
  output wire uart_tx_o,
  output wire uart_de_o
);

  // Internal registers
  reg [7:0]  node_addr;
  reg [31:0] heartbeat_interval;
  reg [1:0]  prio;
  reg        ack_delay;
  
  initial begin
    node_addr <= 8'h01;
    heartbeat_interval <= 32'd250_000_000;
    prio <= 2'b01;
    ack_delay <= 1'b0;
    dat_o <= 32'h0;
  end
  
  // Wishbone address decoding (5-bit address space: 0x00-0x1F)
  wire [4:0] addr = adr_i[4:0];
  wire wb_valid = cyc_i && stb_i;
  
  // Simple loopback: RX -> TX
  assign uart_tx_o = uart_rx_i;
  assign uart_de_o = 1'b1;  // Always drive RS485
  
  // Wishbone logic
  always @(posedge clk_i) begin
    if (rst_i) begin
      node_addr <= 8'h01;
      heartbeat_interval <= 32'd250_000_000;
      prio <= 2'b01;
      ack_delay <= 1'b0;
      dat_o <= 32'h0;
    end else begin
      ack_delay <= wb_valid;  // Standard 1-cycle ack
      
      if (we_i && wb_valid) begin
        case (addr)
          5'h0: begin
            // TX_CMD register (write)
            // Bit layout: [dst_addr(8), len(16), reserved(8)]
            // Trigger transmission (stub: just echo back)
          end
          5'h1: begin
            // TX_DATA register (write)
            // Append payload byte to TX queue
          end
          5'h4: begin
            // CONFIG register (write)
            // [node_addr(8), prio(2), heartbeat_interval(22)]
            node_addr <= dat_i[7:0];
            prio <= dat_i[9:8];
            heartbeat_interval <= dat_i[31:10] << 10;
          end
        endcase
      end
      
      if (wb_valid && !we_i) begin
        case (addr)
          5'h0: dat_o <= 32'h0;  // TX_CMD (write-only)
          5'h1: dat_o <= 32'h0;  // TX_DATA (write-only)
          5'h2: begin
            // STATUS register (read)
            // [reserved(14), RX_valid(1), TX_ready(1), RX_count(8), TX_count(8)]
            dat_o <= {16'h0, 2'b01, 8'h0, 8'h0};  // RX_valid=0, TX_ready=1
          end
          5'h3: dat_o <= 32'h0;  // RX_DATA (read/pop)
          5'h4: dat_o <= {node_addr, prio, 22'h0};  // CONFIG
          default: dat_o <= 32'h0;
        endcase
      end
    end
  end
  
  // Wishbone acknowledge
  assign ack_o = ack_delay;

endmodule
