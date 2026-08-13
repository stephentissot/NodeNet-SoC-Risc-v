`timescale 1ns/1ps

module tb_wb_flash_busy_clear;
  localparam integer CLK_HZ = 25_000_000;
  localparam integer CLK_PERIOD_NS = 40;
  localparam [31:0] FLASH_BASE = 32'h1000_7000;
  // Keep timeout generous in this bench: flash model busy countdown runs on SPI clock edges,
  // while wb_flash timeout counts system clocks.
  localparam [31:0] OP_TIMEOUT_CYCLES = 32'd2000000;

  reg clk = 1'b0;
  reg rst = 1'b1;

  reg [31:0] wb_adr = 32'd0;
  reg [31:0] wb_dat_w = 32'd0;
  wire [31:0] wb_dat_r;
  reg [3:0] wb_sel = 4'hF;
  reg wb_cyc = 1'b0;
  reg wb_stb = 1'b0;
  reg wb_we = 1'b0;
  wire wb_ack;

  wire spi_clk;
  wire spi_mosi;
  wire spi_miso;
  wire spi_cs_n;
  wire [31:0] status_led;
  wire busy;

  wb_flash #(
    .CLOCK_RATE(CLK_HZ),
    .SPI_CLOCK_RATE(10_000_000),
    .ADDR(FLASH_BASE),
    .OP_TIMEOUT_CYCLES(OP_TIMEOUT_CYCLES)
  ) dut (
    .clk_i(clk),
    .rst_i(rst),
    .adr_i(wb_adr),
    .dat_i(wb_dat_w),
    .dat_o(wb_dat_r),
    .sel_i(wb_sel),
    .cyc_i(wb_cyc),
    .stb_i(wb_stb),
    .we_i(wb_we),
    .ack_o(wb_ack),
    .spi_clk_o(spi_clk),
    .spi_mosi_o(spi_mosi),
    .spi_miso_i(spi_miso),
    .spi_cs_n_o(spi_cs_n)
  );

  w25q64jvsiq_model flash_model (
    .cs_n_i(spi_cs_n),
    .clk_i(spi_clk),
    .mosi_i(spi_mosi),
    .miso_o(spi_miso)
  );

  always #(CLK_PERIOD_NS/2) clk = ~clk;

  task automatic wb_write(input [31:0] addr, input [31:0] value);
    begin
      @(posedge clk);
      wb_adr <= addr;
      wb_dat_w <= value;
      wb_we <= 1'b1;
      wb_cyc <= 1'b1;
      wb_stb <= 1'b1;
      while (!wb_ack) @(posedge clk);
      @(posedge clk);
      wb_cyc <= 1'b0;
      wb_stb <= 1'b0;
      wb_we <= 1'b0;
      wb_adr <= 32'd0;
      wb_dat_w <= 32'd0;
    end
  endtask

  task automatic wb_read(input [31:0] addr, output [31:0] value);
    begin
      @(posedge clk);
      wb_adr <= addr;
      wb_we <= 1'b0;
      wb_cyc <= 1'b1;
      wb_stb <= 1'b1;
      while (!wb_ack) @(posedge clk);
      value = wb_dat_r;
      @(posedge clk);
      wb_cyc <= 1'b0;
      wb_stb <= 1'b0;
      wb_adr <= 32'd0;
    end
  endtask

  task automatic wait_for_busy_clear(input integer max_cycles, output bit ok);
    integer cycle;
    bit saw_busy;
    begin
      ok = 1'b0;
      saw_busy = 1'b0;

      for (cycle = 0; cycle < max_cycles; cycle = cycle + 1) begin
        if (!saw_busy && dut.busy) begin
          saw_busy = 1'b1;
          $display("[tb_w25q64_8mb] busy asserted after %0d cycles", cycle + 1);
        end else if (saw_busy && !dut.busy) begin
          ok = 1'b1;
          $display("[tb_w25q64_8mb] busy cleared after %0d cycles", cycle + 1);
          return;
        end

        @(posedge clk);
      end

      if (!saw_busy) begin
        $display("[tb_w25q64_8mb] busy never asserted within %0d cycles", max_cycles);
      end else begin
        $display("[tb_w25q64_8mb] busy did not clear within %0d cycles", max_cycles);
      end
    end
  endtask

  integer i;
  reg [31:0] status_reg;
  bit ok;
  reg [3:0] prev_state = 4'hF;

  initial begin
    $dumpfile("sim/tb_wb_flash_busy_clear_W25Q64_8MB.vcd");
    $dumpvars(0, tb_wb_flash_busy_clear);

    repeat (20) @(posedge clk);
    rst <= 1'b0;
    repeat (20) @(posedge clk);

    wb_read(FLASH_BASE, status_reg);
    $display("[tb_w25q64_8mb] initial status=0x%08h", status_reg);

    wb_write(FLASH_BASE + 32'h0004, 32'h0000_0001);
    wait_for_busy_clear(300000, ok);
    if (!ok) begin
      $display("[tb_w25q64_8mb] READ path stuck");
      $finish;
    end

    wb_write(FLASH_BASE + 32'h0008, 32'h0000_0000);
    for (i = 0; i < 256; i = i + 1) begin
      wb_write(FLASH_BASE + 32'h000C, {24'h0, i[7:0]});
    end
    wb_write(FLASH_BASE + 32'h0004, 32'h0000_0002);
    wait_for_busy_clear(300000, ok);
    if (!ok) begin
      $display("[tb_w25q64_8mb] WRITE path stuck");
      $finish;
    end

    wb_read(FLASH_BASE, status_reg);
    $display("[tb_w25q64_8mb] final status=0x%08h", status_reg);
    $display("[tb_w25q64_8mb] PASS");
    $finish;
  end

  always @(posedge clk) begin
    if (!rst && dut.state != prev_state) begin
      $display("[tb_w25q64_8mb] t=%0t state=%0d busy=%0b timeout=%0d cs_n=%0b clk=%0b",
               $time,
               dut.state,
               dut.busy,
               dut.op_timeout_counter,
               spi_cs_n,
               spi_clk);
      prev_state <= dut.state;
    end
  end

  initial begin
    #20_000_000;
    $display("[tb_w25q64_8mb] TIMEOUT");
    $finish;
  end
endmodule
