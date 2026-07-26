/**
 * @file wb_nodenet.sv
 * @brief NodeNet485 Wishbone Slave Module
 * 
 * Main integration point for NodeNet485 protocol over RS-485 UART.
 * Implements Wishbone B.4 interface and orchestrates TX/RX state machine.
 * 
 * Register Map (offsets from base address):
 *   0x00: TX_CMD (write)     - Initiate message transmission
 *   0x04: TX_DATA (write)    - Queue bytes for transmission
 *   0x08: RX_DATA (read)     - Read received message data
 *   0x0C: STATUS (read)      - Status flags and message counts
 *   0x10: CONFIG (read/write)- Heartbeat interval, node address, priority
 */

`include "nodenet_types.sv"

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

  // ─────────────────────────────────────────────────────────────
  // Internal Signals
  // ─────────────────────────────────────────────────────────────
  
  wire clk = clk_i;
  wire rst_n = ~rst_i;
  
  // TX path
  wire [7:0] tx_fifo_data;
  wire tx_fifo_valid, tx_fifo_ready;
  
  // RX path
  wire [7:0] rx_fifo_data;
  wire rx_fifo_valid, rx_fifo_ready;
  
  // UART layer
  wire uart_rx_valid;
  wire [7:0] uart_rx_data;
  wire uart_tx_ready;
  
  // Decoder output
  wire decoder_msg_valid;
  wire [7:0] decoder_src;
  wire [15:0] decoder_len;
  wire [7:0] decoder_data;
  wire decoder_data_valid;
  wire decoder_complete;
  
  // Configuration registers
  reg [7:0] node_addr;
  reg [31:0] heartbeat_interval;
  reg [1:0] priority;
  
  // Heartbeat
  wire heartbeat_trigger;
  wire [31:0] next_tx_allowed;
  wire heartbeat_msg_send;
  
  // FSM state
  typedef enum logic [2:0] {
    FSM_IDLE,
    FSM_RX_ACTIVE,
    FSM_TX_BACKOFF,
    FSM_TX_ACTIVE,
    FSM_TX_WAIT_COMPLETE
  } fsm_state_t;
  
  fsm_state_t fsm_state;
  
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
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      ack_delayed <= 1'b0;
      node_addr <= 8'h01;  // Default address
      heartbeat_interval <= 32'd250_000_000;  // 10 seconds @ 25MHz
      priority <= 2'b01;  // NORMAL priority
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
            priority <= dat_i[9:8];
            heartbeat_interval <= dat_i[31:10] << 10;  // Scale up
          end
        endcase
      end
      
      // Read transactions
      if (read_en) begin
        case (addr)
          4'h2: dat_o_reg <= {16'b0, 8'b0, rx_fifo_valid, tx_fifo_valid, 5'b0};  // STATUS
          4'h4: dat_o_reg <= {node_addr, priority, 22'b0};  // CONFIG
          default: dat_o_reg <= 32'b0;
        endcase
      end
    end
  end
  
  // ─────────────────────────────────────────────────────────────
  // UART Interface (Simple 8N1 @ 115200 or 19200)
  // ─────────────────────────────────────────────────────────────
  
  uart_simple uart_inst (
    .clk(clk),
    .rst_n(rst_n),
    .rx_i(uart_rx_i),
    .rx_data_o(uart_rx_data),
    .rx_valid_o(uart_rx_valid),
    .tx_o(uart_tx_o),
    .tx_data_i(tx_fifo_data),
    .tx_valid_i(tx_fifo_valid),
    .tx_ready_o(uart_tx_ready),
    .de_o(uart_de_o)  // RS485 driver enable
  );
  
  // ─────────────────────────────────────────────────────────────
  // NodeNet Decoder
  // ─────────────────────────────────────────────────────────────
  
  nodenet_decoder decoder_inst (
    .clk(clk),
    .rst_n(rst_n),
    .my_addr_i(node_addr),
    .rx_byte_valid_i(uart_rx_valid),
    .rx_byte_i(uart_rx_data),
    .msg_valid_o(decoder_msg_valid),
    .msg_src_addr_o(decoder_src),
    .msg_len_o(decoder_len),
    .msg_data_o(decoder_data),
    .msg_data_valid_o(decoder_data_valid),
    .msg_complete_o(decoder_complete),
    .rx_timeout_o(),
    .error_o()
  );
  
  // ─────────────────────────────────────────────────────────────
  // Heartbeat Timer
  // ─────────────────────────────────────────────────────────────
  
  nodenet_heartbeat hb_inst (
    .clk(clk),
    .rst_n(rst_n),
    .heartbeat_interval_cycles(heartbeat_interval),
    .node_addr(node_addr),
    .message_sent_i(1'b0),  // TODO: connect from TX FSM
    .is_broadcast_i(1'b0),   // TODO: connect from TX FSM
    .heartbeat_trigger_o(heartbeat_trigger),
    .next_transmit_allowed_o(next_tx_allowed)
  );
  
  // ─────────────────────────────────────────────────────────────
  // TX/RX State Machine
  // ─────────────────────────────────────────────────────────────
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      fsm_state <= FSM_IDLE;
    end
    else begin
      case (fsm_state)
        FSM_IDLE: begin
          if (uart_rx_valid) begin
            fsm_state <= FSM_RX_ACTIVE;
          end
          else if (heartbeat_trigger) begin
            // TODO: Generate heartbeat message
            fsm_state <= FSM_TX_ACTIVE;
          end
        end
        
        FSM_RX_ACTIVE: begin
          if (decoder_complete) begin
            fsm_state <= FSM_IDLE;
          end
        end
        
        FSM_TX_BACKOFF: begin
          // Wait until next_tx_allowed cycle count
          fsm_state <= FSM_TX_ACTIVE;
        end
        
        FSM_TX_ACTIVE: begin
          if (uart_tx_ready) begin
            fsm_state <= FSM_TX_WAIT_COMPLETE;
          end
        end
        
        FSM_TX_WAIT_COMPLETE: begin
          if (!uart_tx_ready) begin
            fsm_state <= FSM_IDLE;
          end
        end
        
        default: fsm_state <= FSM_IDLE;
      endcase
    end
  end
  
  // Placeholder for FIFO instances
  // assign tx_fifo_valid = 1'b0;
  // assign rx_fifo_valid = 1'b0;
  // assign irq_o = 1'b0;

endmodule
