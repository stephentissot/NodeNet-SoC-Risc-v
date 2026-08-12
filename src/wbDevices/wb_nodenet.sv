/**
 * @file wb_nodenet.sv
 * @brief NodeNet485 Wishbone B.4 Slave with MMIO mailboxes
 *
 * The original design described SDRAM-backed FIFOs, but this block has no
 * direct SDRAM master port. This implementation finalizes the peripheral as a
 * self-contained mailbox transport that the firmware can drive entirely through
 * Wishbone registers while still exercising the real wire protocol.
 */

`include "src/wbDevices/nodenet_defines.vh"

module wb_nodenet #(
  parameter [31:0] CLOCK_RATE = 25_000_000
) (
  input  wire clk_i,
  input  wire rst_i,
  input  wire [31:0] adr_i,
  input  wire [31:0] dat_i,
  output reg  [31:0] dat_o,
  input  wire we_i,
  input  wire [3:0] sel_i,
  input  wire cyc_i,
  input  wire stb_i,
  output reg ack_o,
  input  wire uart_rx_i,
  output wire uart_tx_o,
  output wire tx_led_o,
  output wire rx_led_o
);

  localparam integer MAX_PAYLOAD = `NODENET_MAX_PAYLOAD_SIZE;

  localparam [3:0]
    REG_TX_CMD  = 4'd0,
    REG_TX_DATA = 4'd1,
    REG_RX_HDR  = 4'd2,
    REG_RX_DATA = 4'd3,
    REG_CONFIG  = 4'd4,
    REG_CONTROL = 4'd5,
    REG_STATUS  = 4'd6,
    REG_LED_CFG = 4'd7,
    REG_UART_BAUD = 4'd8;

  localparam [31:0] DEFAULT_ACTIVITY_BLINK_MS = 32'd100;
  localparam [31:0] CYCLES_PER_MS = CLOCK_RATE / 32'd1000;

  localparam [3:0]
    TX_IDLE       = 4'd0,
    TX_PREFIX_LF  = 4'd1,
    TX_PREFIX_SOH = 4'd2,
    TX_HDR_DST    = 4'd3,
    TX_HDR_SRC    = 4'd4,
    TX_LEN_HI     = 4'd5,
    TX_LEN_LO     = 4'd6,
    TX_STX        = 4'd7,
    TX_PAYLOAD_HI = 4'd8,
    TX_PAYLOAD_LO = 4'd9,
    TX_ETX        = 4'd10,
    TX_CRC        = 4'd11,
    TX_EOT        = 4'd12,
    TX_SUFFIX_LF  = 4'd13,
    TX_TRAILER_ZERO = 4'd14;

  function [7:0] encode_nibble_high;
    input [7:0] byte_in;
    reg [7:0] high_nib;
    begin
      high_nib = byte_in & 8'hF0;
      encode_nibble_high = high_nib | ((~high_nib & 8'hF0) >> 4);
    end
  endfunction

  function [7:0] encode_nibble_low;
    input [7:0] byte_in;
    reg [7:0] low_nib;
    begin
      low_nib = byte_in & 8'h0F;
      encode_nibble_low = low_nib | ((~low_nib) << 4);
    end
  endfunction

  function [31:0] compute_tx_delay;
    input [7:0] dst_addr;
    input [1:0] prio_sel;
    input [7:0] src_addr;
    reg [31:0] delay_cycles;
    begin
      if (dst_addr == 8'h00)
        delay_cycles = {24'h0, src_addr} * `NODENET_BROADCAST_DELAY_PER_ADDR;
      else
        delay_cycles = `NODENET_LINE_READY_CYCLES + ({24'h0, src_addr} * `NODENET_UNICAST_DELAY_PER_ADDR);

      case (prio_sel)
        2'b10: delay_cycles = delay_cycles >> 1;
        2'b00: delay_cycles = delay_cycles + (delay_cycles >> 1);
        default: begin end
      endcase

      compute_tx_delay = delay_cycles;
    end
  endfunction

  wire rst_n = ~rst_i;
  wire wb_valid = cyc_i && stb_i;
  wire wb_fire = wb_valid && !ack_o;
  wire [3:0] reg_index = adr_i[5:2];

  reg [7:0] node_addr;
  reg hb_enabled;
  reg [31:0] heartbeat_interval;
  reg [1:0] prio;
  reg [31:0] activity_blink_ms;
  reg [19:0] uart_divisor;

  reg [7:0] tx_stage_dst;
  reg [15:0] tx_stage_len;
  reg [15:0] tx_load_count;
  reg tx_stage_valid;
  reg tx_pending;
  reg tx_pending_is_heartbeat;
  reg tx_frame_is_heartbeat;
  reg tx_active;
  reg [7:0] tx_frame_dst;
  reg [15:0] tx_frame_len;
  reg [15:0] tx_stream_idx;
  reg [1:0] tx_prefix_lf_count;
  reg [7:0] tx_crc;
  reg [7:0] tx_current_byte;
  reg [3:0] tx_state;
  reg [31:0] tx_cooldown_counter;
  reg last_tx_was_broadcast;

  reg [7:0] rx_src;
  reg [15:0] rx_len;
  reg [15:0] rx_read_idx;
  reg [15:0] rx_build_count;
  reg rx_valid;
  reg rx_overflow;
  reg rx_error_sticky;
  reg rx_uart_seen_sticky;
  reg rx_uart_frame_error_sticky;

  reg [7:0] tx_buffer [0:MAX_PAYLOAD-1];
  reg [7:0] rx_buffer [0:MAX_PAYLOAD-1];

  reg [7:0] uart_tx_data;
  reg uart_tx_valid;
  reg message_sent_pulse;
  reg tx_led_trigger_pulse;
  reg rx_led_trigger_pulse;

  wire [7:0] uart_rx_data;
  wire uart_rx_valid;
  wire uart_tx_ready;
  wire uart_de;
  wire uart_rx_frame_error;
  reg uart_tx_ready_d;
  reg [23:0] rx_ignore_counter;
  wire rx_decode_enable;

  wire decoder_msg_valid;
  wire [7:0] decoder_msg_src;
  wire [15:0] decoder_msg_len;
  wire [7:0] decoder_msg_data;
  wire decoder_msg_data_valid;
  wire decoder_msg_complete;
  wire decoder_timeout;
  wire decoder_error;

  wire heartbeat_trigger;
  wire [31:0] unused_next_transmit_allowed;
  wire [31:0] activity_blink_cycles;
  wire [23:0] rx_ignore_reload;

  assign activity_blink_cycles = ((activity_blink_ms != 32'd0) ? activity_blink_ms : DEFAULT_ACTIVITY_BLINK_MS) * CYCLES_PER_MS;
  // Ignore RX for ~16 bit-times after local TX to avoid RS485 self-echo tails.
  assign rx_ignore_reload = {4'b0000, uart_divisor} << 4;
  assign rx_decode_enable = !uart_de && (rx_ignore_counter == 24'd0);

  uart_simple #(
    .CLOCK_RATE(CLOCK_RATE),
    .BAUD_RATE(1_000_000),
    .USE_DIVISOR_INPUT(1'b1)
  ) nodenet_uart (
    .clk(clk_i),
    .rst_n(rst_n),
    .prescale_i(16'd0),
    .divisor_i(uart_divisor),
    .rx_i(uart_rx_i),
    .rx_data_o(uart_rx_data),
    .rx_valid_o(uart_rx_valid),
    .tx_o(uart_tx_o),
    .tx_data_i(uart_tx_data),
    .tx_valid_i(uart_tx_valid),
    .tx_ready_o(uart_tx_ready),
    .rx_frame_error_o(uart_rx_frame_error),
    .de_o(uart_de)
  );

  nodenet_decoder nodenet_rx (
    .clk(clk_i),
    .rst_n(rst_n),
    .my_addr_i(node_addr),
    // Ignore local echo while TX is active and briefly after TX release.
    .rx_byte_valid_i(uart_rx_valid && rx_decode_enable),
    .rx_byte_i(uart_rx_data),
    .msg_valid_o(decoder_msg_valid),
    .msg_src_addr_o(decoder_msg_src),
    .msg_len_o(decoder_msg_len),
    .msg_data_o(decoder_msg_data),
    .msg_data_valid_o(decoder_msg_data_valid),
    .msg_complete_o(decoder_msg_complete),
    .rx_timeout_o(decoder_timeout),
    .error_o(decoder_error)
  );

  nodenet_heartbeat #(
    .CLOCK_RATE(CLOCK_RATE)
  ) nodenet_hb (
    .clk(clk_i),
    .rst_n(rst_n),
    .heartbeat_interval_cycles(heartbeat_interval),
    .node_addr(node_addr),
    .message_sent_i(message_sent_pulse),
    .is_broadcast_i(last_tx_was_broadcast),
    .heartbeat_trigger_o(heartbeat_trigger),
    .next_transmit_allowed_o(unused_next_transmit_allowed)
  );

  led_pulse_core #(
    .ACTIVE_LOW(1'b1),
    .DEFAULT_STATE(1'b0),
    .BLINK_CYCLES(32'd2500000)
  ) tx_activity_led (
    .clk(clk_i),
    .rst(rst_i),
    .trigger_i(tx_led_trigger_pulse),
    .set_default_i(1'b0),
    .default_value_i(1'b0),
    .blink_cycles_i(activity_blink_cycles),
    .led_o(tx_led_o),
    .busy_o(),
    .default_state_o()
  );

  led_pulse_core #(
    .ACTIVE_LOW(1'b1),
    .DEFAULT_STATE(1'b1),
    .BLINK_CYCLES(32'd2500000)
  ) rx_activity_led (
    .clk(clk_i),
    .rst(rst_i),
    .trigger_i(rx_led_trigger_pulse),
    .set_default_i(1'b0),
    .default_value_i(1'b1),
    .blink_cycles_i(activity_blink_cycles),
    .led_o(rx_led_o),
    .busy_o(),
    .default_state_o()
  );

  always @(posedge clk_i) begin
    uart_tx_valid <= 1'b0;
    message_sent_pulse <= 1'b0;
    tx_led_trigger_pulse <= 1'b0;
    rx_led_trigger_pulse <= 1'b0;

    if (rst_i) begin
      node_addr <= 8'h01;
      hb_enabled <= 1'b0;
      heartbeat_interval <= `NODENET_HEARTBEAT_DEFAULT_CYCLES;
      prio <= 2'b01;
      activity_blink_ms <= DEFAULT_ACTIVITY_BLINK_MS;
      uart_divisor <= (CLOCK_RATE / 1_000_000);
      tx_stage_dst <= 8'h00;
      tx_stage_len <= 16'h0000;
      tx_load_count <= 16'h0000;
      tx_stage_valid <= 1'b0;
      tx_pending <= 1'b0;
      tx_pending_is_heartbeat <= 1'b0;
      tx_frame_is_heartbeat <= 1'b0;
      tx_active <= 1'b0;
      tx_frame_dst <= 8'h00;
      tx_frame_len <= 16'h0000;
      tx_stream_idx <= 16'h0000;
      tx_prefix_lf_count <= 2'b00;
      tx_crc <= 8'h00;
      tx_current_byte <= 8'h00;
      tx_state <= TX_IDLE;
      tx_cooldown_counter <= 32'h0000_0000;
      last_tx_was_broadcast <= 1'b0;
      rx_src <= 8'h00;
      rx_len <= 16'h0000;
      rx_read_idx <= 16'h0000;
      rx_build_count <= 16'h0000;
      rx_valid <= 1'b0;
      rx_overflow <= 1'b0;
      rx_error_sticky <= 1'b0;
      rx_uart_seen_sticky <= 1'b0;
      rx_uart_frame_error_sticky <= 1'b0;
      dat_o <= 32'h0000_0000;
      uart_tx_data <= 8'hFF;
      ack_o <= 1'b0;
      uart_tx_ready_d <= 1'b0;
      rx_ignore_counter <= 24'd0;
    end else begin
      ack_o <= wb_valid;
      uart_tx_ready_d <= uart_tx_ready;

      if (uart_de) begin
        rx_ignore_counter <= rx_ignore_reload;
      end else if (rx_ignore_counter != 24'd0) begin
        rx_ignore_counter <= rx_ignore_counter - 24'd1;
      end

      if (tx_cooldown_counter != 32'h0000_0000)
        tx_cooldown_counter <= tx_cooldown_counter - 32'h0000_0001;

      if (decoder_msg_data_valid) begin
        if (!rx_valid && (rx_build_count < MAX_PAYLOAD)) begin
          rx_buffer[rx_build_count] <= decoder_msg_data;
          rx_build_count <= rx_build_count + 16'h0001;
        end else begin
          rx_overflow <= 1'b1;
        end
      end

      if (uart_rx_valid)
        rx_uart_seen_sticky <= 1'b1;

      if (uart_rx_frame_error) begin
        rx_uart_frame_error_sticky <= 1'b1;
      end

      if (decoder_msg_complete) begin
        if (decoder_msg_valid && !rx_valid && (decoder_msg_len <= MAX_PAYLOAD)) begin
          rx_src <= decoder_msg_src;
          rx_read_idx <= 16'h0000;
          rx_valid <= 1'b1;
          rx_led_trigger_pulse <= 1'b1;

          // Report an extra byte for C-string terminator when there is room.
          // The terminator is synthesized on read path, not stored in rx_buffer.
          if (decoder_msg_len < MAX_PAYLOAD) begin
            rx_len <= decoder_msg_len + 16'h0001;
          end else begin
            rx_len <= decoder_msg_len;
          end
        end else if (decoder_msg_valid) begin
          rx_overflow <= 1'b1;
        end
        rx_build_count <= 16'h0000;
      end

      if (decoder_timeout || decoder_error) begin
        rx_build_count <= 16'h0000;
        // Timeout can happen on line noise/partial framing and is exposed
        // as a pulse in STATUS. Keep sticky error for real decode faults only.
        if (decoder_error && rx_decode_enable)
          rx_error_sticky <= 1'b1;
      end

      if (hb_enabled && heartbeat_trigger && !tx_stage_valid && !tx_pending && !tx_active) begin
        tx_pending <= 1'b1;
        tx_pending_is_heartbeat <= 1'b1;
        tx_cooldown_counter <= compute_tx_delay(8'h00, prio, node_addr);
        tx_led_trigger_pulse <= 1'b1;
      end

      if (wb_fire) begin
        if (we_i) begin
          case (reg_index)
            REG_TX_CMD: begin
              if (!tx_active && !tx_pending) begin
                tx_stage_dst <= dat_i[31:24];
                tx_stage_len <= dat_i[15:0];
                tx_load_count <= 16'h0000;
                tx_stage_valid <= (dat_i[15:0] <= MAX_PAYLOAD);
                tx_pending_is_heartbeat <= 1'b0;
              end
            end

            REG_TX_DATA: begin
              if (tx_stage_valid && !tx_active && !tx_pending && (tx_load_count < tx_stage_len) && (tx_load_count < MAX_PAYLOAD)) begin
                tx_buffer[tx_load_count] <= dat_i[7:0];
                tx_load_count <= tx_load_count + 16'h0001;
              end
            end

            REG_CONFIG: begin
              node_addr <= dat_i[7:0];
              hb_enabled <= (dat_i[7:0] != 8'h00);
              prio <= dat_i[9:8];
              // Keep the existing/default heartbeat interval when firmware
              // only updates addr/prio (dat_i[31:10] == 0).
              if (dat_i[31:10] != 22'd0)
                heartbeat_interval <= {dat_i[31:10], 10'b0};
            end

            REG_LED_CFG: begin
              activity_blink_ms <= dat_i;
            end

            REG_UART_BAUD: begin
              if (dat_i[19:0] != 20'd0)
                uart_divisor <= dat_i[19:0];
            end

            REG_CONTROL: begin
              if (dat_i[1]) begin
                rx_valid <= 1'b0;
                rx_read_idx <= 16'h0000;
                rx_overflow <= 1'b0;
                rx_error_sticky <= 1'b0;
                rx_uart_seen_sticky <= 1'b0;
                rx_uart_frame_error_sticky <= 1'b0;
                rx_build_count <= 16'h0000;
              end

              if (hb_enabled && dat_i[2] && !tx_stage_valid && !tx_pending && !tx_active) begin
                tx_pending <= 1'b1;
                tx_pending_is_heartbeat <= 1'b1;
                tx_cooldown_counter <= compute_tx_delay(8'h00, prio, node_addr);
                tx_led_trigger_pulse <= 1'b1;
              end

              if (dat_i[0] && tx_stage_valid && !tx_pending && !tx_active && (tx_load_count == tx_stage_len)) begin
                tx_pending <= 1'b1;
                tx_pending_is_heartbeat <= 1'b0;
                tx_cooldown_counter <= compute_tx_delay(tx_stage_dst, prio, node_addr);
                tx_led_trigger_pulse <= 1'b1;
              end
            end

            default: begin end
          endcase
        end else begin
          case (reg_index)
            REG_TX_CMD: begin
              dat_o <= {tx_stage_dst, tx_stage_valid, tx_pending, tx_active, 5'b0, tx_stage_len};
            end

            REG_TX_DATA: begin
              dat_o <= {16'h0000, tx_load_count};
            end

            REG_RX_HDR: begin
              dat_o <= {rx_src, 7'b0, rx_valid, rx_len};
            end

            REG_RX_DATA: begin
              if (rx_valid) begin
                if ((rx_len != 16'h0000) && (rx_read_idx == (rx_len - 16'h0001)))
                  dat_o <= 32'h0000_0000; // synthetic '\0' terminator byte
                else
                  dat_o <= {24'h000000, rx_buffer[rx_read_idx]};
              end else begin
                dat_o <= 32'h0000_0000;
              end

              if (rx_valid) begin
                if (rx_read_idx + 16'h0001 >= rx_len) begin
                  rx_valid <= 1'b0;
                  rx_read_idx <= 16'h0000;
                end else begin
                  rx_read_idx <= rx_read_idx + 16'h0001;
                end
              end
            end

            REG_CONFIG: begin
              dat_o <= {heartbeat_interval[31:10], prio, node_addr};
            end

            REG_CONTROL: begin
              dat_o <= 32'h0000_0000;
            end

            REG_STATUS: begin
              dat_o <= {
                rx_overflow,
                rx_error_sticky,
                (hb_enabled && heartbeat_trigger),
                (tx_cooldown_counter != 32'h0000_0000),
                tx_pending,
                tx_active,
                uart_tx_ready,
                rx_valid,
                tx_stage_valid,
                (tx_stage_valid && (tx_load_count == tx_stage_len)),
                uart_de,
                decoder_timeout,
                decoder_error,
                rx_uart_seen_sticky,
                rx_uart_frame_error_sticky,
                1'b0,
                tx_load_count[15:0]
              };
            end

            REG_LED_CFG: begin
              dat_o <= activity_blink_ms;
            end

            REG_UART_BAUD: begin
              dat_o <= {12'h000, uart_divisor};
            end

            default: begin
              dat_o <= 32'h0000_0000;
            end
          endcase
        end
      end

      if (!tx_active && tx_pending && (tx_cooldown_counter == 32'h0000_0000)) begin
        tx_pending <= 1'b0;
        tx_active <= 1'b1;
        uart_tx_ready_d <= 1'b0;
        tx_frame_is_heartbeat <= tx_pending_is_heartbeat;
        tx_state <= TX_PREFIX_LF;
        tx_prefix_lf_count <= 2'b00;
        tx_stream_idx <= 16'h0000;

        if (tx_pending_is_heartbeat) begin
          tx_frame_dst <= 8'h00;
          tx_frame_len <= 16'h0000;
          tx_crc <= node_addr;
          last_tx_was_broadcast <= 1'b1;
        end else begin
          tx_frame_dst <= tx_stage_dst;
          tx_frame_len <= tx_stage_len;
          tx_crc <= tx_stage_dst ^ node_addr ^ tx_stage_len[15:8] ^ tx_stage_len[7:0];
          last_tx_was_broadcast <= (tx_stage_dst == 8'h00);
        end
      end

      // Drive one TX byte per ready rising edge to keep handshake aligned.
      if (tx_active && uart_tx_ready && !uart_tx_ready_d) begin
        case (tx_state)
          TX_PREFIX_LF: begin
            uart_tx_data <= `NODENET_LF;
            uart_tx_valid <= 1'b1;
            if (tx_prefix_lf_count == 2'd2) begin
              tx_state <= TX_PREFIX_SOH;
            end else begin
              tx_prefix_lf_count <= tx_prefix_lf_count + 2'd1;
              tx_state <= TX_PREFIX_LF;
            end
          end

          TX_PREFIX_SOH: begin
            uart_tx_data <= `NODENET_SOH;
            uart_tx_valid <= 1'b1;
            tx_state <= TX_HDR_DST;
          end

          TX_HDR_DST: begin
            uart_tx_data <= tx_frame_dst;
            uart_tx_valid <= 1'b1;
            tx_state <= TX_HDR_SRC;
          end

          TX_HDR_SRC: begin
            uart_tx_data <= node_addr;
            uart_tx_valid <= 1'b1;
            tx_state <= TX_LEN_HI;
          end

          TX_LEN_HI: begin
            uart_tx_data <= tx_frame_len[15:8];
            uart_tx_valid <= 1'b1;
            tx_state <= TX_LEN_LO;
          end

          TX_LEN_LO: begin
            uart_tx_data <= tx_frame_len[7:0];
            uart_tx_valid <= 1'b1;
            if (tx_frame_is_heartbeat)
              tx_state <= TX_EOT;
            else
              tx_state <= TX_STX;
          end

          TX_STX: begin
            uart_tx_data <= `NODENET_STX;
            uart_tx_valid <= 1'b1;

            if (tx_frame_len == 16'h0000)
              tx_state <= TX_ETX;
            else
              tx_state <= TX_PAYLOAD_HI;
          end

          TX_PAYLOAD_HI: begin
            tx_current_byte <= tx_buffer[tx_stream_idx];
            uart_tx_data <= encode_nibble_high(tx_buffer[tx_stream_idx]);
            uart_tx_valid <= 1'b1;
            tx_state <= TX_PAYLOAD_LO;
          end

          TX_PAYLOAD_LO: begin
            uart_tx_data <= encode_nibble_low(tx_current_byte);
            uart_tx_valid <= 1'b1;
            tx_crc <= tx_crc ^ tx_current_byte;

            if (tx_stream_idx + 16'h0001 >= tx_frame_len) begin
              tx_stream_idx <= 16'h0000;
              tx_state <= TX_ETX;
            end else begin
              tx_stream_idx <= tx_stream_idx + 16'h0001;
              tx_state <= TX_PAYLOAD_HI;
            end
          end

          TX_ETX: begin
            uart_tx_data <= `NODENET_ETX;
            uart_tx_valid <= 1'b1;
            tx_state <= TX_CRC;
          end

          TX_CRC: begin
            if (tx_frame_is_heartbeat) begin
              uart_tx_data <= 8'h00;
              uart_tx_valid <= 1'b1;
              tx_state <= TX_IDLE;
              tx_active <= 1'b0;
              tx_frame_is_heartbeat <= 1'b0;
              tx_stage_valid <= 1'b0;
              tx_pending_is_heartbeat <= 1'b0;
              tx_stage_dst <= 8'h00;
              tx_stage_len <= 16'h0000;
              tx_load_count <= 16'h0000;
              message_sent_pulse <= 1'b1;
            end else begin
              uart_tx_data <= tx_crc;
              uart_tx_valid <= 1'b1;
              tx_state <= TX_EOT;
            end
          end

          TX_EOT: begin
            uart_tx_data <= `NODENET_EOT;
            uart_tx_valid <= 1'b1;
            if (tx_frame_is_heartbeat)
              tx_state <= TX_CRC;
            else
              tx_state <= TX_TRAILER_ZERO;
          end

          TX_TRAILER_ZERO: begin
            // Keep wire behavior aligned with heartbeat trailer convention.
            uart_tx_data <= 8'h00;
            uart_tx_valid <= 1'b1;
            tx_state <= TX_SUFFIX_LF;
          end

          TX_SUFFIX_LF: begin
            uart_tx_data <= `NODENET_LF;
            uart_tx_valid <= 1'b1;
            tx_state <= TX_IDLE;
            tx_active <= 1'b0;
            tx_frame_is_heartbeat <= 1'b0;
            tx_stage_valid <= 1'b0;
            tx_pending_is_heartbeat <= 1'b0;
            tx_stage_dst <= 8'h00;
            tx_stage_len <= 16'h0000;
            tx_load_count <= 16'h0000;
            message_sent_pulse <= 1'b1;
          end

          default: begin
            tx_state <= TX_IDLE;
            tx_active <= 1'b0;
          end
        endcase
      end
    end
  end

endmodule

