`timescale 1ns/1ps

module tb_nodenet_heartbeat_frame;
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

  reg uart_rx_i = 1'b1;
  wire uart_tx_o;
  wire tx_led_o;
  wire rx_led_o;

  integer captured = 0;

  wb_nodenet #(
    .CLOCK_RATE(25_000_000)
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
    .uart_rx_i(uart_rx_i),
    .uart_tx_o(uart_tx_o),
    .tx_led_o(tx_led_o),
    .rx_led_o(rx_led_o)
  );

  always #20 clk = ~clk; // 25 MHz

  task wb_write;
    input [31:0] addr;
    input [31:0] data;
    begin
      @(posedge clk);
      adr_i <= addr;
      dat_i <= data;
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

  initial begin
    repeat (8) @(posedge clk);
    rst <= 1'b0;

    // Make UART byte-time short for simulation speed.
    wb_write(32'h1000_6020, 32'd1); // REG_UART_BAUD divisor

    // Configure addr=0x41, prio=NORMAL(1), heartbeat interval field=1 => 1024 cycles.
    wb_write(32'h1000_6010, (32'd1 << 10) | (32'd1 << 8) | 32'h41);

    // Force one immediate heartbeat TX in simulation (bypass backoff wait).
    @(posedge clk);
    dut.tx_pending <= 1'b1;
    dut.tx_pending_is_heartbeat <= 1'b1;
    dut.tx_cooldown_counter <= 32'd0;

    // Wait long enough to capture all bytes.
    repeat (5000) @(posedge clk);

    $display("Timeout waiting heartbeat frame");
    $finish;
  end

  always @(posedge clk) begin
    if (!rst && dut.uart_tx_valid) begin
      $display("TX[%0d]=0x%02h", captured, dut.uart_tx_data);
      captured = captured + 1;

      if (captured == 10) begin
        $display("Captured 10 bytes, ending simulation.");
        $finish;
      end
    end
  end
endmodule
