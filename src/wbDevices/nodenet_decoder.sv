/**
 * @file nodenet_decoder.sv
 * @brief NodeNet485 Protocol Decoder
 */

`include "src/wbDevices/nodenet_defines.vh"

module nodenet_decoder (
  input wire clk,
  input wire rst_n,
  
  // Configuration
  input wire [7:0] my_addr_i,  // My NodeNet address
  
  // UART RX input
  input wire rx_byte_valid_i,
  input wire [7:0] rx_byte_i,
  
  // Message output (to RX FIFO)
  output reg msg_valid_o,
  output reg [7:0] msg_src_addr_o,
  output reg [15:0] msg_len_o,
  output reg [7:0] msg_data_o,
  output reg msg_data_valid_o,
  output reg msg_complete_o,
  
  // Status
  output wire rx_timeout_o,
  output reg error_o
);

  // State machine for reception
  typedef enum logic [3:0] {
    IDLE,
    WAIT_SOH,
    RX_DST,
    RX_SRC,
    RX_LEN_HI,
    RX_LEN_LO,
    WAIT_STX,
    RX_PAYLOAD,
    RX_ETX,
    RX_CRC,
    RX_EOT,
    VALIDATE
  } state_t;
  
  state_t state;
  
  reg [7:0] dst, src;
  reg [15:0] payload_len, payload_idx;
  reg [7:0] crc, crc_received;
  reg [7:0] high_nibble;
  reg first_nibble;
  reg [7:0] decoded_byte;
  
  // Timeout counter
  reg [31:0] timeout_counter;
  wire timeout = (timeout_counter > `NODENET_RECEIVE_TIMEOUT_CYCLES);
  
  assign rx_timeout_o = timeout && (state != IDLE);
  
  // Encoded nibble checker: high/low nibbles must be bitwise complements.
  function is_valid_encoded_nibble(input [7:0] byte_in);
    reg [3:0] high, low;
    high = byte_in[7:4];
    low = byte_in[3:0];
    is_valid_encoded_nibble = ((high ^ low) == 4'hf);
  endfunction
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state <= IDLE;
      timeout_counter <= 32'b0;
      msg_valid_o <= 1'b0;
      msg_data_valid_o <= 1'b0;
      msg_complete_o <= 1'b0;
      first_nibble <= 1'b1;
      crc <= 8'b0;
      error_o <= 1'b0;
    end
    else begin
      msg_valid_o <= 1'b0;
      msg_data_valid_o <= 1'b0;
      msg_complete_o <= 1'b0;
      error_o <= 1'b0;
      
      if (rx_byte_valid_i) begin
        timeout_counter <= 32'b0;  // Reset on byte arrival
        
        case (state)
          IDLE: begin
            if (rx_byte_i == `NODENET_SOH) begin
              state <= RX_DST;
              crc <= 8'b0;
            end
          end
          
          RX_DST: begin
            dst <= rx_byte_i;
            state <= RX_SRC;
          end
          
          RX_SRC: begin
            src <= rx_byte_i;
            state <= RX_LEN_HI;
          end
          
          RX_LEN_HI: begin
            payload_len[15:8] <= rx_byte_i;
            state <= RX_LEN_LO;
          end
          
          RX_LEN_LO: begin
            payload_len[7:0] <= rx_byte_i;
            crc <= dst ^ src ^ payload_len[15:8] ^ rx_byte_i;
            payload_idx <= 16'b0;
            state <= WAIT_STX;
          end
          
          WAIT_STX: begin
            if (rx_byte_i == `NODENET_STX) begin
              first_nibble <= 1'b1;
              state <= RX_PAYLOAD;
            end
            else if (rx_byte_i == `NODENET_EOT) begin
              // Heartbeat: no payload
              msg_src_addr_o <= src;
              msg_len_o <= 16'b0;
              msg_complete_o <= 1'b1;
              msg_valid_o <= 1'b1;
              state <= IDLE;
            end
          end
          
          RX_PAYLOAD: begin
            if (is_valid_encoded_nibble(rx_byte_i)) begin
              if (first_nibble) begin
                high_nibble <= (rx_byte_i & 8'hf0);
                first_nibble <= 1'b0;
              end
              else begin
                decoded_byte <= high_nibble | (rx_byte_i & 8'h0f);
                msg_data_o <= high_nibble | (rx_byte_i & 8'h0f);
                msg_data_valid_o <= 1'b1;
                crc <= crc ^ (high_nibble | (rx_byte_i & 8'h0f));
                payload_idx <= payload_idx + 16'b1;
                first_nibble <= 1'b1;
                
                if (payload_idx + 16'b1 == payload_len) begin
                  state <= RX_ETX;
                end
              end
            end
            else if (rx_byte_i == `NODENET_ETX && payload_idx == payload_len) begin
              state <= RX_CRC;
            end
            else begin
              // Error: invalid nibble or ETX mismatch
              error_o <= 1'b1;
              state <= IDLE;
            end
          end
          
          RX_CRC: begin
            crc_received <= rx_byte_i;
            state <= RX_EOT;
          end
          
          RX_EOT: begin
            if (rx_byte_i == `NODENET_EOT) begin
              state <= VALIDATE;
            end
            else begin
              error_o <= 1'b1;
              state <= IDLE;
            end
          end
          
          VALIDATE: begin
            if (crc == crc_received && (dst == my_addr_i || dst == 8'b0)) begin
              msg_src_addr_o <= src;
              msg_len_o <= payload_len;
              msg_complete_o <= 1'b1;
              msg_valid_o <= 1'b1;
            end
            else begin
              error_o <= 1'b1;
            end
            state <= IDLE;
          end
          
          default: state <= IDLE;
        endcase
      end
      else begin
        timeout_counter <= timeout_counter + 32'b1;
        if (timeout && (state != IDLE)) begin
          error_o <= 1'b1;
          state <= IDLE;
          first_nibble <= 1'b1;
        end
      end
    end
  end

endmodule
