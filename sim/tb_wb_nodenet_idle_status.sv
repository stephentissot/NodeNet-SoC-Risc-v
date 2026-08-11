`timescale 1ns/1ps

module tb_wb_nodenet_idle_status;
  localparam integer CLK_HZ = 25_000_000;
  localparam integer CLK_PERIOD_NS = 40;

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

  wire uart_tx_o;
  wire tx_led_o;
  wire rx_led_o;

  // No external RX traffic.
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
    .uart_rx_i(1'b1),
    .uart_tx_o(uart_tx_o),
    .tx_led_o(tx_led_o),
    .rx_led_o(rx_led_o)
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

  reg [31:0] st;
  integer i;
  initial begin
    repeat (20) @(posedge clk);
    rst <= 1'b0;
    repeat (20) @(posedge clk);

    // Set Node address + enable heartbeat + short heartbeat interval.
    // heartbeat_interval = ({dat[31:10],10'b0}) = (10'd244 << 10) = 249856 cycles.
    wb_write(32'h0000_0010, (32'd244 << 10) | (32'd1 << 8) | 32'h41);
    wb_write(32'h0000_0020, 32'd217); // 115200 divisor
    wb_write(32'h0000_0014, 32'h0000_0002); // clear RX/error

    for (i = 0; i < 20; i = i + 1) begin
      repeat (80000) @(posedge clk);
      wb_read(32'h0000_0018, st);
      $display("[TB] t=%0t st=%08X E=%0d F=%0d U=%0d hb_due=%0d tx_p=%0d tx_a=%0d dec_err=%0d dec_to=%0d",
        $time,
        st,
        st[30], st[17], st[18], st[29], st[27], st[26], st[19], st[20]
      );
    end

    $finish;
  end
endmodule
