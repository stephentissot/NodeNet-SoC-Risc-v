/**
 * @file wb_modbus_master.sv
 * @brief Wishbone Modbus RTU Master (minimal transaction engine)
 *
 * Livrable A scope:
 * - Modbus RTU request/response transaction engine
 * - CRC16 generation/check (polynomial 0xA001)
 * - End-of-frame detection using silent interval (t3.5-like)
 * - Retry handling
 * - Wishbone status/control interface
 *
 * Notes:
 * - TX request format: [slave][function][data...][crc_lo][crc_hi]
 * - RX response buffer stores raw RTU frame bytes.
 */

module wb_modbus_master #(
  parameter [31:0] CLOCK_RATE = 25_000_000,
  parameter [19:0] DEFAULT_UART_DIVISOR = 20'd217,
  parameter [31:0] DEFAULT_TIMEOUT_CYCLES = 32'd2_500_000,
  parameter [31:0] DEFAULT_INTERFRAME_CYCLES = 32'd8_680
) (
  input  wire        clk_i,
  input  wire        rst_i,

  input  wire [31:0] adr_i,
  input  wire [31:0] dat_i,
  output reg  [31:0] dat_o,
  input  wire        we_i,
  input  wire [3:0]  sel_i,
  input  wire        cyc_i,
  input  wire        stb_i,
  output reg         ack_o,

  input  wire        uart_rx_i,
  output wire        uart_tx_o,
  output wire        uart_de_o
);

  localparam integer MAX_DATA_BYTES = 252;
  localparam integer MAX_FRAME_BYTES = 256;

  localparam [3:0]
    REG_CONTROL     = 4'd0,
    REG_STATUS      = 4'd1,
    REG_UART_DIV    = 4'd2,
    REG_SLAVE_FUNC  = 4'd3,
    REG_TIMEOUT     = 4'd4,
    REG_INTERFRAME  = 4'd5,
    REG_RETRY       = 4'd6,
    REG_TX_LEN      = 4'd7,
    REG_TX_DATA     = 4'd8,
    REG_RX_LEN      = 4'd9,
    REG_RX_DATA     = 4'd10,
    REG_DEBUG       = 4'd11;

  localparam [3:0]
    ST_IDLE             = 4'd0,
    ST_WAIT_PRE_TX_GAP  = 4'd1,
    ST_TX_STREAM         = 4'd2,
    ST_WAIT_RX_START     = 4'd3,
    ST_WAIT_RX_END       = 4'd4,
    ST_EVAL_RESPONSE     = 4'd5,
    ST_DONE              = 4'd6;

  function [15:0] crc16_step;
    input [15:0] crc_in;
    input [7:0] data_in;
    integer bit_idx;
    reg [15:0] c;
    begin
      c = crc_in ^ {8'h00, data_in};
      for (bit_idx = 0; bit_idx < 8; bit_idx = bit_idx + 1) begin
        if (c[0])
          c = (c >> 1) ^ 16'hA001;
        else
          c = (c >> 1);
      end
      crc16_step = c;
    end
  endfunction

  wire wb_valid = cyc_i && stb_i;
  wire wb_fire = wb_valid && !ack_o;
  wire [3:0] reg_index = adr_i[5:2];

  reg [19:0] uart_divisor;
  reg [31:0] timeout_cycles;
  reg [31:0] interframe_cycles;
  reg [7:0] slave_addr;
  reg [7:0] function_code;
  reg [7:0] retry_max;
  reg [7:0] tx_data_len;

  reg [7:0] tx_payload [0:MAX_DATA_BYTES-1];
  reg [7:0] rx_frame [0:MAX_FRAME_BYTES-1];

  reg [8:0] tx_total_len;
  reg [8:0] tx_stream_idx;
  reg [8:0] rx_count;
  reg [8:0] rx_read_idx;
  reg [7:0] tx_write_idx;

  reg [31:0] timeout_ctr;
  reg [31:0] gap_ctr;

  reg [7:0] retry_count;

  reg [3:0] state;

  reg status_done;
  reg status_success;
  reg status_timeout;
  reg status_crc_error;
  reg status_frame_error;
  reg status_exception;
  reg status_rx_overflow;
  reg status_uart_frame_error;
  reg status_retrying;

  reg [15:0] tx_crc_accum;
  reg [15:0] tx_crc_final;
  reg [15:0] rx_crc_full;

  reg [15:0] debug_crc_calc;
  reg [15:0] debug_crc_rx;

  reg [7:0] uart_tx_data;
  reg uart_tx_valid;
  wire uart_tx_ready;
  wire [7:0] uart_rx_data;
  wire uart_rx_valid;
  wire uart_rx_frame_error;

  wire busy = (state != ST_IDLE) && (state != ST_DONE);

  uart_simple #(
    .CLOCK_RATE(CLOCK_RATE),
    .BAUD_RATE(115200),
    .USE_DIVISOR_INPUT(1'b1)
  ) modbus_uart (
    .clk(clk_i),
    .rst_n(~rst_i),
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
    .de_o(uart_de_o)
  );

  task automatic clear_status_flags;
    begin
      status_done <= 1'b0;
      status_success <= 1'b0;
      status_timeout <= 1'b0;
      status_crc_error <= 1'b0;
      status_frame_error <= 1'b0;
      status_exception <= 1'b0;
      status_rx_overflow <= 1'b0;
      status_uart_frame_error <= 1'b0;
      status_retrying <= 1'b0;
    end
  endtask

  task automatic reset_rx_capture;
    begin
      rx_count <= 9'd0;
      rx_read_idx <= 9'd0;
      timeout_ctr <= 32'd0;
      gap_ctr <= 32'd0;
      rx_crc_full <= 16'hFFFF;
    end
  endtask

  task automatic start_retry_or_finish;
    input mark_timeout;
    input mark_crc;
    input mark_frame;
    begin
      if (mark_timeout)
        status_timeout <= 1'b1;
      if (mark_crc)
        status_crc_error <= 1'b1;
      if (mark_frame)
        status_frame_error <= 1'b1;

      if (retry_count < retry_max) begin
        retry_count <= retry_count + 8'd1;
        status_retrying <= 1'b1;
        reset_rx_capture();
        tx_stream_idx <= 9'd0;
        state <= ST_WAIT_PRE_TX_GAP;
      end else begin
        status_done <= 1'b1;
        status_success <= 1'b0;
        status_retrying <= 1'b0;
        state <= ST_DONE;
      end
    end
  endtask

  always @(posedge clk_i) begin
    ack_o <= wb_fire;

    if (rst_i) begin
      dat_o <= 32'd0;

      uart_divisor <= DEFAULT_UART_DIVISOR;
      timeout_cycles <= DEFAULT_TIMEOUT_CYCLES;
      interframe_cycles <= DEFAULT_INTERFRAME_CYCLES;
      slave_addr <= 8'd1;
      function_code <= 8'd3;
      retry_max <= 8'd0;
      tx_data_len <= 8'd0;
      tx_write_idx <= 8'd0;
      tx_total_len <= 9'd0;
      tx_stream_idx <= 9'd0;
      rx_count <= 9'd0;
      rx_read_idx <= 9'd0;
      timeout_ctr <= 32'd0;
      gap_ctr <= 32'd0;
      retry_count <= 8'd0;
      state <= ST_IDLE;

      tx_crc_accum <= 16'hFFFF;
      tx_crc_final <= 16'hFFFF;
      rx_crc_full <= 16'hFFFF;

      status_done <= 1'b0;
      status_success <= 1'b0;
      status_timeout <= 1'b0;
      status_crc_error <= 1'b0;
      status_frame_error <= 1'b0;
      status_exception <= 1'b0;
      status_rx_overflow <= 1'b0;
      status_uart_frame_error <= 1'b0;
      status_retrying <= 1'b0;

      debug_crc_calc <= 16'd0;
      debug_crc_rx <= 16'd0;
      uart_tx_data <= 8'd0;
      uart_tx_valid <= 1'b0;
    end else begin
      if (uart_rx_frame_error)
        status_uart_frame_error <= 1'b1;

      if (wb_fire) begin
        case (reg_index)
          REG_CONTROL: begin
            if (we_i) begin
              if (sel_i[0] && dat_i[1]) begin
                clear_status_flags();
              end

              if (sel_i[0] && dat_i[2]) begin
                state <= ST_IDLE;
                tx_stream_idx <= 9'd0;
                uart_tx_valid <= 1'b0;
                retry_count <= 8'd0;
                reset_rx_capture();
                status_done <= 1'b1;
                status_success <= 1'b0;
                status_retrying <= 1'b0;
              end

              if (sel_i[0] && dat_i[0] && (state == ST_IDLE || state == ST_DONE)) begin
                clear_status_flags();
                retry_count <= 8'd0;
                tx_stream_idx <= 9'd0;
                tx_write_idx <= 8'd0;

                tx_crc_final <= tx_crc_accum;
                tx_total_len <= {1'b0, tx_data_len} + 9'd4;
                debug_crc_calc <= tx_crc_accum;

                reset_rx_capture();
                state <= ST_WAIT_PRE_TX_GAP;
              end
            end

            dat_o <= {
              29'd0,
              (state == ST_DONE),
              (state == ST_IDLE),
              busy
            };
          end

          REG_STATUS: begin
            dat_o <= {
              8'd0,
              rx_count[7:0],
              retry_count,
              5'd0,
              status_retrying,
              status_uart_frame_error,
              status_rx_overflow,
              status_exception,
              status_frame_error,
              status_crc_error,
              status_timeout,
              status_success,
              status_done,
              busy
            };
          end

          REG_UART_DIV: begin
            dat_o <= {12'd0, uart_divisor};
            if (we_i) begin
              if (sel_i[0]) uart_divisor[7:0] <= dat_i[7:0];
              if (sel_i[1]) uart_divisor[15:8] <= dat_i[15:8];
              if (sel_i[2]) uart_divisor[19:16] <= dat_i[19:16];
            end
          end

          REG_SLAVE_FUNC: begin
            dat_o <= {16'd0, slave_addr, function_code};
            if (we_i) begin
              if (sel_i[0]) function_code <= dat_i[7:0];
              if (sel_i[1]) slave_addr <= dat_i[15:8];
              if ((sel_i[0] || sel_i[1]) && (tx_write_idx == 8'd0)) begin
                tx_crc_accum <= crc16_step(crc16_step(16'hFFFF, (sel_i[1] ? dat_i[15:8] : slave_addr)),
                                           (sel_i[0] ? dat_i[7:0] : function_code));
              end
            end
          end

          REG_TIMEOUT: begin
            dat_o <= timeout_cycles;
            if (we_i) begin
              if (sel_i[0]) timeout_cycles[7:0] <= dat_i[7:0];
              if (sel_i[1]) timeout_cycles[15:8] <= dat_i[15:8];
              if (sel_i[2]) timeout_cycles[23:16] <= dat_i[23:16];
              if (sel_i[3]) timeout_cycles[31:24] <= dat_i[31:24];
            end
          end

          REG_INTERFRAME: begin
            dat_o <= interframe_cycles;
            if (we_i) begin
              if (sel_i[0]) interframe_cycles[7:0] <= dat_i[7:0];
              if (sel_i[1]) interframe_cycles[15:8] <= dat_i[15:8];
              if (sel_i[2]) interframe_cycles[23:16] <= dat_i[23:16];
              if (sel_i[3]) interframe_cycles[31:24] <= dat_i[31:24];
            end
          end

          REG_RETRY: begin
            dat_o <= {24'd0, retry_max};
            if (we_i && sel_i[0])
              retry_max <= dat_i[7:0];
          end

          REG_TX_LEN: begin
            dat_o <= {24'd0, tx_data_len};
            if (we_i && sel_i[0] && !busy) begin
              if (dat_i[7:0] <= MAX_DATA_BYTES[7:0])
                tx_data_len <= dat_i[7:0];
              else
                tx_data_len <= MAX_DATA_BYTES[7:0];
              tx_write_idx <= 8'd0;
              tx_crc_accum <= crc16_step(crc16_step(16'hFFFF, slave_addr), function_code);
            end
          end

          REG_TX_DATA: begin
            dat_o <= {24'd0, tx_write_idx};
            if (we_i && sel_i[0] && !busy) begin
              if ((tx_write_idx < tx_data_len) && (tx_write_idx < MAX_DATA_BYTES[7:0])) begin
                tx_payload[tx_write_idx] <= dat_i[7:0];
                tx_crc_accum <= crc16_step(tx_crc_accum, dat_i[7:0]);
                tx_write_idx <= tx_write_idx + 8'd1;
              end
            end
          end

          REG_RX_LEN: begin
            dat_o <= {23'd0, rx_count};
            if (we_i && sel_i[0])
              rx_read_idx <= 9'd0;
          end

          REG_RX_DATA: begin
            if (rx_read_idx < rx_count) begin
              dat_o <= {24'd0, rx_frame[rx_read_idx]};
              rx_read_idx <= rx_read_idx + 9'd1;
            end else begin
              dat_o <= 32'd0;
            end
          end

          REG_DEBUG: begin
            dat_o <= {debug_crc_rx, debug_crc_calc};
          end

          default: begin
            dat_o <= 32'd0;
          end
        endcase
      end

      case (state)
        ST_IDLE: begin
          uart_tx_valid <= 1'b0;
          gap_ctr <= 32'd0;
          timeout_ctr <= 32'd0;
        end

        ST_WAIT_PRE_TX_GAP: begin
          uart_tx_valid <= 1'b0;
          if (uart_rx_valid) begin
            gap_ctr <= 32'd0;
          end else if (gap_ctr < interframe_cycles) begin
            gap_ctr <= gap_ctr + 32'd1;
          end else begin
            gap_ctr <= 32'd0;
            tx_stream_idx <= 9'd0;
            state <= ST_TX_STREAM;
          end
        end

        ST_TX_STREAM: begin
          if (!uart_tx_valid && (tx_stream_idx < tx_total_len)) begin
            if (uart_tx_ready) begin
              if (tx_stream_idx == 9'd0) begin
                uart_tx_data <= slave_addr;
              end else if (tx_stream_idx == 9'd1) begin
                uart_tx_data <= function_code;
              end else if (tx_stream_idx < ({1'b0, tx_data_len} + 9'd2)) begin
                uart_tx_data <= tx_payload[tx_stream_idx - 9'd2];
              end else if (tx_stream_idx == ({1'b0, tx_data_len} + 9'd2)) begin
                uart_tx_data <= tx_crc_final[7:0];
              end else begin
                uart_tx_data <= tx_crc_final[15:8];
              end
              uart_tx_valid <= 1'b1;
            end
          end else if (uart_tx_valid) begin
            // uart_simple samples tx_valid/data on the clock edge that starts
            // transmission. Hold the byte until tx_ready drops, then advance.
            if (!uart_tx_ready) begin
              uart_tx_valid <= 1'b0;
              tx_stream_idx <= tx_stream_idx + 9'd1;
            end
          end else if ((tx_stream_idx >= tx_total_len) && uart_tx_ready) begin
            timeout_ctr <= 32'd0;
            gap_ctr <= 32'd0;
            state <= ST_WAIT_RX_START;
          end
        end

        ST_WAIT_RX_START: begin
          uart_tx_valid <= 1'b0;
          if (uart_rx_valid) begin
            rx_frame[0] <= uart_rx_data;
            rx_count <= 9'd1;
            rx_crc_full <= crc16_step(16'hFFFF, uart_rx_data);
            timeout_ctr <= 32'd0;
            gap_ctr <= 32'd0;
            state <= ST_WAIT_RX_END;
          end else if (timeout_ctr < timeout_cycles) begin
            timeout_ctr <= timeout_ctr + 32'd1;
          end else begin
            start_retry_or_finish(1'b1, 1'b0, 1'b0);
          end
        end

        ST_WAIT_RX_END: begin
          uart_tx_valid <= 1'b0;
          if (uart_rx_valid) begin
            gap_ctr <= 32'd0;
            rx_crc_full <= crc16_step(rx_crc_full, uart_rx_data);
            if (rx_count < MAX_FRAME_BYTES[8:0]) begin
              rx_frame[rx_count] <= uart_rx_data;
              rx_count <= rx_count + 9'd1;
            end else begin
              status_rx_overflow <= 1'b1;
            end
          end else if (gap_ctr < interframe_cycles) begin
            gap_ctr <= gap_ctr + 32'd1;
          end else begin
            state <= ST_EVAL_RESPONSE;
          end

          if (timeout_ctr < timeout_cycles)
            timeout_ctr <= timeout_ctr + 32'd1;
          else
            start_retry_or_finish(1'b1, 1'b0, 1'b0);
        end

        ST_EVAL_RESPONSE: begin
          uart_tx_valid <= 1'b0;
          if (rx_count < 9'd4) begin
            start_retry_or_finish(1'b0, 1'b0, 1'b1);
          end else begin
            debug_crc_calc <= rx_crc_full;
            debug_crc_rx <= {rx_frame[rx_count - 9'd1], rx_frame[rx_count - 9'd2]};

            if (rx_crc_full != 16'h0000) begin
              start_retry_or_finish(1'b0, 1'b1, 1'b0);
            end else if ((slave_addr != 8'h00) && (rx_frame[0] != slave_addr)) begin
              start_retry_or_finish(1'b0, 1'b0, 1'b1);
            end else begin
              if (rx_frame[1] == (function_code | 8'h80)) begin
                status_done <= 1'b1;
                status_success <= 1'b0;
                status_exception <= 1'b1;
                status_retrying <= 1'b0;
                state <= ST_DONE;
              end else if (rx_frame[1] != function_code) begin
                start_retry_or_finish(1'b0, 1'b0, 1'b1);
              end else begin
                status_done <= 1'b1;
                status_success <= 1'b1;
                status_retrying <= 1'b0;
                state <= ST_DONE;
              end
            end
          end
        end

        ST_DONE: begin
          uart_tx_valid <= 1'b0;
          // Wait for a new START command or explicit clear/abort via CONTROL.
        end

        default: begin
          state <= ST_IDLE;
        end
      endcase
    end
  end

endmodule
