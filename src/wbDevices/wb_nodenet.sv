/**
 * @file wb_nodenet.sv
 * @brief NodeNet485 Wishbone Slave with SDRAM-based FIFO
 * 
 * SDRAM FIFO Layout (1 MB reserved):
 *   0x20000000 - 0x2007FFFF : TX ring buffer (512 KB)
 *   0x20080000 - 0x200FFFFF : RX ring buffer (512 KB)
 * 
 * Ring buffer format: [dst(8)|len_hi(8)|len_lo(8)|payload(N bytes)]
 * Firmware manages read/write pointers via registers.
 */

module wb_nodenet #(
  parameter CLOCK_RATE = 25_000_000,
  parameter SDRAM_TX_BASE = 32'h20000000,  // TX FIFO base
  parameter SDRAM_RX_BASE = 32'h20080000,  // RX FIFO base
  parameter SDRAM_FIFO_SIZE = 32'h80000    // 512 KB per FIFO
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
  output wire uart_tx_o
);

  // Internal state
  reg [7:0]  node_addr;
  reg [31:0] heartbeat_interval;
  reg [1:0]  prio;
  
  // SDRAM FIFO pointers (managed by firmware)
  // TX: next byte to transmit
  // RX: next byte to receive
  reg [31:0] tx_write_ptr;  // Firmware writes here
  reg [31:0] tx_read_ptr;   // Hardware reads from here
  reg [31:0] rx_write_ptr;  // Hardware writes here
  reg [31:0] rx_read_ptr;   // Firmware reads from here
  
  // Status flags
  reg tx_active;
  reg rx_valid;
  
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
  
  // Wishbone address decoding
  wire [4:0] addr = adr_i[4:0];
  wire wb_valid = cyc_i && stb_i;
  
  // Simple loopback for now (RX -> TX)
  assign uart_tx_o = uart_rx_i;
  assign ack_o = wb_valid;
  
  // Register read/write
  always @(posedge clk_i) begin
    if (rst_i) begin
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
        case (addr)
          5'h0: tx_write_ptr <= dat_i;    // TX_WRITE_PTR (firmware sets)
          5'h1: tx_read_ptr <= dat_i;     // TX_READ_PTR (firmware clears after TX)
          5'h2: rx_write_ptr <= dat_i;    // RX_WRITE_PTR (cleared by HW after RX, if needed)
          5'h3: rx_read_ptr <= dat_i;     // RX_READ_PTR (firmware advances)
          5'h4: begin
            // CONFIG: [node_addr(8), prio(2), hb_interval(22)]
            node_addr <= dat_i[7:0];
            prio <= dat_i[9:8];
            heartbeat_interval <= dat_i[31:10] << 10;
          end
          5'h5: begin
            // CONTROL: [reserved(31), trigger_tx(1)]
            if (dat_i[0]) tx_active <= 1'b1;
          end
        endcase
      end else begin
        case (addr)
          5'h0: dat_o <= tx_write_ptr;
          5'h1: dat_o <= tx_read_ptr;
          5'h2: dat_o <= rx_write_ptr;
          5'h3: dat_o <= rx_read_ptr;
          5'h4: dat_o <= {heartbeat_interval[21:0], prio, node_addr};
          5'h5: begin
            // STATUS: [reserved(22), rx_valid(1), tx_active(1), tx_avail(4), rx_avail(4)]
            dat_o <= {22'h0, rx_valid, tx_active, 
                      (tx_write_ptr != tx_read_ptr) ? 4'hF : 4'h0,
                      (rx_write_ptr != rx_read_ptr) ? 4'hF : 4'h0};
          end
          default: dat_o <= 32'h0;
        endcase
      end
    end
  end
  
  // Wishbone acknowledge
  assign ack_o = wb_valid;

endmodule
