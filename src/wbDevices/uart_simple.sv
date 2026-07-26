/**
 * @file uart_simple.sv
 * @brief Simple UART 8N1 Transceiver
 * 
 * Basic UART implementation for use with NodeNet485.
 * Supports configurable baud rate via divisor.
 * Default: 115200 @ 25MHz
 */

module uart_simple #(
  parameter CLOCK_RATE = 25_000_000,
  parameter BAUD_RATE = 115200
) (
  input wire clk,
  input wire rst_n,
  
  // RX Interface
  input wire rx_i,
  output reg [7:0] rx_data_o,
  output reg rx_valid_o,
  
  // TX Interface
  output wire tx_o,
  input wire [7:0] tx_data_i,
  input wire tx_valid_i,
  output wire tx_ready_o,
  
  // RS485 Driver Enable
  output wire de_o  // Driver enable (high when transmitting)
);

  localparam DIVISOR = CLOCK_RATE / BAUD_RATE;
  
  // TX side
  reg [7:0] tx_buffer;
  reg [9:0] tx_shift;
  reg [19:0] tx_counter;
  reg tx_busy;
  reg [3:0] tx_bit_count;
  
  assign tx_ready_o = ~tx_busy;
  assign tx_o = tx_shift[0];
  assign de_o = tx_busy;  // RS485 driver enable while transmitting
  
  // RX side (stub - simplified)
  reg [7:0] rx_shift;
  reg [19:0] rx_counter;
  reg [3:0] rx_bit_count;
  reg rx_busy;
  
  // ─────────────────────────────────────────────────────────────
  // TX Logic
  // ─────────────────────────────────────────────────────────────
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      tx_busy <= 1'b0;
      tx_shift <= 10'b1111111111;  // Idle high
      tx_counter <= 20'b0;
      tx_bit_count <= 4'b0;
    end
    else begin
      rx_valid_o <= 1'b0;
      
      if (!tx_busy && tx_valid_i) begin
        // Start transmission
        tx_buffer <= tx_data_i;
        tx_shift <= {1'b1, tx_data_i, 1'b0};  // Stop + data + start bit
        tx_bit_count <= 4'd10;
        tx_counter <= 20'b0;
        tx_busy <= 1'b1;
      end
      else if (tx_busy) begin
        if (tx_counter >= DIVISOR - 1) begin
          tx_counter <= 20'b0;
          tx_shift <= {1'b1, tx_shift[9:1]};
          tx_bit_count <= tx_bit_count - 4'b1;
          
          if (tx_bit_count == 4'd1) begin
            tx_busy <= 1'b0;
          end
        end
        else begin
          tx_counter <= tx_counter + 20'b1;
        end
      end
    end
  end
  
  // ─────────────────────────────────────────────────────────────
  // RX Logic (Stub - real implementation would need proper FSM)
  // ─────────────────────────────────────────────────────────────
  
  reg rx_sync1, rx_sync2;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      rx_sync1 <= 1'b1;
      rx_sync2 <= 1'b1;
    end
    else begin
      rx_sync1 <= rx_i;
      rx_sync2 <= rx_sync1;
    end
  end
  
  // Simple RX (edge detection)
  reg rx_prev;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      rx_prev <= 1'b1;
      rx_shift <= 8'b0;
      rx_counter <= 20'b0;
      rx_bit_count <= 4'b0;
      rx_busy <= 1'b0;
      rx_data_o <= 8'b0;
    end
    else begin
      rx_prev <= rx_sync2;
      
      if (!rx_busy && !rx_sync2 && rx_prev) begin
        // Start bit detected
        rx_counter <= DIVISOR / 2;
        rx_bit_count <= 4'd8;
        rx_busy <= 1'b1;
      end
      else if (rx_busy) begin
        if (rx_counter >= DIVISOR - 1) begin
          rx_counter <= 20'b0;
          rx_shift <= {rx_sync2, rx_shift[7:1]};
          rx_bit_count <= rx_bit_count - 4'b1;
          
          if (rx_bit_count == 4'd1) begin
            rx_busy <= 1'b0;
            rx_data_o <= rx_shift;
            rx_valid_o <= 1'b1;
          end
        end
        else begin
          rx_counter <= rx_counter + 20'b1;
        end
      end
    end
  end

endmodule
