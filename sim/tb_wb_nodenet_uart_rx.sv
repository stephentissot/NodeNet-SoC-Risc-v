`timescale 1ns/1ps

module tb_wb_nodenet_uart_rx;
  localparam integer CLK_HZ = 25_000_000;
  localparam integer CLK_PERIOD_NS = 40;
  localparam [19:0] UART_DIV_115200 = 20'd217;

  reg clk = 1'b0;
  reg rst = 1'b1;

  reg [31:0] adr_i = 32'd0;
  reg [31:0] dat_i = 32'd0;
  wire [31:0] dat_o;
  reg we_i = 1'b0;
  reg [3:0] sel_i = 4'hF;
  reg cyc_i = 1'b0;
  reg stb_i = 1'b0;
  wire ack_o;

  wire nodenet_tx_o;
  wire tx_led_o;
  wire rx_led_o;

  wire stim_tx_o;
  reg [7:0] stim_tx_data = 8'h00;
  reg stim_tx_valid = 1'b0;
  wire stim_tx_ready;

  // UART sender output drives wb_nodenet RX input directly.
  wb_nodenet #(
    .CLOCK_RATE(CLK_HZ)
  ) dut (
    .clk_i(clk),
    .rst_i(rst),
    .adr_i(adr_i),
    .dat_i(dat_i),
    .dat_o(dat_o),
    .we_i(we_i),
    .sel_i(sel_i),
    .cyc_i(cyc_i),
    .stb_i(stb_i),
    .ack_o(ack_o),
    .uart_rx_i(stim_tx_o),
    .uart_tx_o(nodenet_tx_o),
    .tx_led_o(tx_led_o),
    .rx_led_o(rx_led_o)
  );

  uart_simple #(
    .CLOCK_RATE(CLK_HZ),
    .BAUD_RATE(115200),
    .USE_DIVISOR_INPUT(1'b1)
  ) stim_uart (
    .clk(clk),
    .rst_n(~rst),
    .prescale_i(16'd0),
    .divisor_i(UART_DIV_115200),
    .rx_i(1'b1),
    .rx_data_o(),
    .rx_valid_o(),
    .tx_o(stim_tx_o),
    .tx_data_i(stim_tx_data),
    .tx_valid_i(stim_tx_valid),
    .tx_ready_o(stim_tx_ready),
    .rx_frame_error_o(),
    .de_o()
  );

  always #(CLK_PERIOD_NS/2) clk = ~clk;

  task automatic wb_write(input [31:0] addr, input [31:0] value);
    begin
      @(posedge clk);
      adr_i <= addr;
      dat_i <= value;
      we_i <= 1'b1;
      cyc_i <= 1'b1;
      stb_i <= 1'b1;
      while (!ack_o) @(posedge clk);
      @(posedge clk);
      cyc_i <= 1'b0;
      stb_i <= 1'b0;
      we_i <= 1'b0;
      adr_i <= 32'd0;
      dat_i <= 32'd0;
    end
  endtask

  task automatic wb_read(input [31:0] addr, output [31:0] value);
    begin
      @(posedge clk);
      adr_i <= addr;
      we_i <= 1'b0;
      cyc_i <= 1'b1;
      stb_i <= 1'b1;
      while (!ack_o) @(posedge clk);
      value = dat_o;
      @(posedge clk);
      cyc_i <= 1'b0;
      stb_i <= 1'b0;
      adr_i <= 32'd0;
    end
  endtask

  task automatic uart_send_byte(input [7:0] b);
    begin
      while (!stim_tx_ready) @(posedge clk);
      stim_tx_data <= b;
      stim_tx_valid <= 1'b1;
      @(posedge clk);
      // Keep valid asserted until transmitter consumes the byte
      // (tx_ready drops when tx_busy starts).
      while (stim_tx_ready) @(posedge clk);
      @(posedge clk);
      stim_tx_valid <= 1'b0;
    end
  endtask

  integer i;
  reg [7:0] frame [0:21];
  reg [31:0] status;
  reg [31:0] rx_hdr;

  initial begin
    // Frame provided by user (corrected length: 00 04).
    frame[0]  = 8'h0A;
    frame[1]  = 8'h0A;
    frame[2]  = 8'h0A;
    frame[3]  = 8'h01;
    frame[4]  = 8'h41;
    frame[5]  = 8'h02;
    frame[6]  = 8'h00;
    frame[7]  = 8'h04;
    frame[8]  = 8'h02;
    frame[9]  = 8'h5A;
    frame[10] = 8'hB4;
    frame[11] = 8'h4B;
    frame[12] = 8'hA5;
    frame[13] = 8'h5A;
    frame[14] = 8'hC3;
    frame[15] = 8'h5A;
    frame[16] = 8'hB4;
    frame[17] = 8'h03;
    frame[18] = 8'h51;
    frame[19] = 8'h04;
    frame[20] = 8'h0A;
    frame[21] = 8'h0A;

    repeat (20) @(posedge clk);
    rst <= 1'b0;
    repeat (20) @(posedge clk);

    // Match firmware setup: addr=0x41, priority=normal (1), baud divisor=217.
    wb_write(32'h0000_0010, 32'h0000_0141); // REG_CONFIG
    wb_write(32'h0000_0020, 32'd217);       // REG_UART_BAUD
    wb_write(32'h0000_0014, 32'h0000_0002); // REG_CONTROL clear RX/errors

    $display("[TB] Sending frame bytes...");
    for (i = 0; i < 22; i = i + 1) begin
      uart_send_byte(frame[i]);
    end

    // Wait for any late bookkeeping.
    repeat (5000) @(posedge clk);

    wb_read(32'h0000_0018, status); // REG_STATUS
    wb_read(32'h0000_0008, rx_hdr); // REG_RX_HDR

    $display("[TB] STATUS = 0x%08X", status);
    $display("[TB] RX_HDR = 0x%08X", rx_hdr);
    $display("[TB] rx_valid=%0d, rx_src=0x%02X, rx_len=%0d", dut.rx_valid, dut.rx_src, dut.rx_len);
    $display("[TB] dbg: uart_seen=%0d uart_frame_err=%0d dec_err=%0d dec_timeout=%0d msg_complete=%0d msg_valid=%0d",
      dut.rx_uart_seen_sticky,
      dut.rx_uart_frame_error_sticky,
      dut.decoder_error,
      dut.decoder_timeout,
      dut.decoder_msg_complete,
      dut.decoder_msg_valid
    );

    if (dut.rx_valid) begin
      $display("[TB] RX payload bytes:");
      for (i = 0; i < dut.rx_len; i = i + 1) begin
        $display("  rx[%0d] = 0x%02X", i, dut.rx_buffer[i]);
      end
    end

    $finish;
  end

  always @(posedge clk) begin
    if (dut.uart_rx_valid)
      $display("[TB] uart_rx byte=0x%02X t=%0t", dut.uart_rx_data, $time);

    if (dut.decoder_msg_data_valid)
      $display("[TB] decoded payload byte=0x%02X idx=%0d", dut.decoder_msg_data, dut.rx_build_count);

    if (dut.decoder_msg_complete)
      $display("[TB] decoder complete valid=%0d src=0x%02X len=%0d", dut.decoder_msg_valid, dut.decoder_msg_src, dut.decoder_msg_len);

    if (dut.decoder_error)
      $display("[TB] decoder_error pulse t=%0t", $time);

    if (dut.uart_rx_frame_error)
      $display("[TB] uart_rx_frame_error pulse t=%0t", $time);
  end

endmodule
