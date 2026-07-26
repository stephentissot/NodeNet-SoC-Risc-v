/**
 * @file nodenet_encoder.sv
 * @brief NodeNet485 Protocol Encoder
 */

`include "src/wbDevices/nodenet_defines.vh"

module nodenet_encoder (
  input wire clk,
  input wire rst_n,
  
  // Message input interface (strobed)
  input wire valid_i,
  input wire [7:0] dst_addr_i,
  input wire [7:0] src_addr_i,
  input wire [15:0] payload_len_i,
  input wire [7:0] payload_i,
  input wire payload_valid_i,
  
  // Encoded output interface
  output reg [7:0] data_o,
  output reg data_valid_o,
  output reg tx_complete_o
);

  // State machine
  typedef enum logic [3:0] {
    IDLE,
    PREFIX_LF,
    PREFIX_SOH,
    HDR_DST,
    HDR_SRC,
    HDR_LEN_HI,
    HDR_LEN_LO,
    PREFIX_STX,
    ENCODE_PAYLOAD,
    SUFFIX_ETX,
    SUFFIX_CRC,
    SUFFIX_EOT,
    SUFFIX_LF
  } state_t;
  
  state_t state, next_state;
  
  reg [7:0] dst, src;
  reg [15:0] payload_len, payload_idx;
  reg [7:0] crc;
  reg [7:0] current_byte;
  reg [1:0] nibble_count;  // 0=high, 1=low
  
  // Nibble encoder: b | (~b[7:4] >> 4) for high nibble, b | (~b[3:0] << 4) for low nibble
  function [7:0] encode_nibble_high(input [7:0] byte_in);
    reg [7:0] high_nib;
    high_nib = (byte_in & 8'hf0);
    encode_nibble_high = high_nib | ((~high_nib & 8'hf0) >> 4);
  endfunction
  
  function [7:0] encode_nibble_low(input [7:0] byte_in);
    reg [7:0] low_nib;
    low_nib = (byte_in & 8'h0f);
    encode_nibble_low = low_nib | ((~low_nib) << 4);
  endfunction
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state <= IDLE;
      data_o <= 8'b0;
      data_valid_o <= 1'b0;
      tx_complete_o <= 1'b0;
      crc <= 8'b0;
    end
    else begin
      data_valid_o <= 1'b0;
      tx_complete_o <= 1'b0;
      
      case (state)
        IDLE: begin
          if (valid_i) begin
            dst <= dst_addr_i;
            src <= src_addr_i;
            payload_len <= payload_len_i;
            payload_idx <= 16'b0;
            crc <= dst_addr_i ^ src_addr_i ^ (payload_len_i >> 8) ^ payload_len_i;
            state <= PREFIX_LF;
          end
        end
        
        PREFIX_LF: begin
          data_o <= `NODENET_LF;
          data_valid_o <= 1'b1;
          state <= PREFIX_SOH;
        end
        
        PREFIX_SOH: begin
          data_o <= `NODENET_SOH;
          data_valid_o <= 1'b1;
          state <= HDR_DST;
        end
        
        HDR_DST: begin
          data_o <= dst;
          data_valid_o <= 1'b1;
          state <= HDR_SRC;
        end
        
        HDR_SRC: begin
          data_o <= src;
          data_valid_o <= 1'b1;
          state <= HDR_LEN_HI;
        end
        
        HDR_LEN_HI: begin
          data_o <= payload_len[15:8];
          data_valid_o <= 1'b1;
          state <= HDR_LEN_LO;
        end
        
        HDR_LEN_LO: begin
          data_o <= payload_len[7:0];
          data_valid_o <= 1'b1;
          state <= PREFIX_STX;
        end
        
        PREFIX_STX: begin
          data_o <= `NODENET_STX;
          data_valid_o <= 1'b1;
          nibble_count <= 2'b0;
          state <= ENCODE_PAYLOAD;
        end
        
        ENCODE_PAYLOAD: begin
          if (payload_valid_i && payload_idx < payload_len) begin
            if (nibble_count == 2'b0) begin
              current_byte <= payload_i;
              data_o <= encode_nibble_high(payload_i);
              data_valid_o <= 1'b1;
              nibble_count <= 2'b1;
            end
            else begin
              data_o <= encode_nibble_low(current_byte);
              data_valid_o <= 1'b1;
              crc <= crc ^ current_byte;
              payload_idx <= payload_idx + 16'b1;
              nibble_count <= 2'b0;
            end
          end
          else if (payload_idx == payload_len) begin
            state <= SUFFIX_ETX;
          end
        end
        
        SUFFIX_ETX: begin
          data_o <= `NODENET_ETX;
          data_valid_o <= 1'b1;
          state <= SUFFIX_CRC;
        end
        
        SUFFIX_CRC: begin
          data_o <= crc;
          data_valid_o <= 1'b1;
          state <= SUFFIX_EOT;
        end
        
        SUFFIX_EOT: begin
          data_o <= `NODENET_EOT;
          data_valid_o <= 1'b1;
          state <= SUFFIX_LF;
        end
        
        SUFFIX_LF: begin
          data_o <= `NODENET_LF;
          data_valid_o <= 1'b1;
          tx_complete_o <= 1'b1;
          state <= IDLE;
        end
        
        default: state <= IDLE;
      endcase
    end
  end

endmodule
