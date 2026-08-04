
`timescale 1ns/1ps
module tb;
  reg clk = 0;
  reg rst = 1;
  reg [31:0] wb_adr_i = 0;
  reg [31:0] wb_dat_i = 0;
  reg [3:0] wb_sel_i = 4'hf;
  reg wb_we_i = 0;
  reg wb_cyc_i = 0;
  reg wb_stb_i = 0;
  wire wb_ack_o;
  wire [31:0] wb_dat_o;
  wire i2c_scl;
  wire i2c_sda;
  pullup(i2c_scl);
  pullup(i2c_sda);
  wb_i2c_zipcpu #(.ADDR(32'h10005000), .TICKBITS(6'd20), .CLOCKS_PER_TICK(20'd2), .MEM_ADDR_BITS(8)) dut (
    .clk(clk), .rst(rst),
    .wb_adr_i(wb_adr_i), .wb_dat_i(wb_dat_i), .wb_dat_o(wb_dat_o),
    .wb_sel_i(wb_sel_i), .wb_we_i(wb_we_i), .wb_cyc_i(wb_cyc_i), .wb_stb_i(wb_stb_i),
    .wb_ack_o(wb_ack_o),
    .i2c_scl(i2c_scl), .i2c_sda(i2c_sda)
  );
  always #5 clk = ~clk;
  always @(posedge clk) begin
    if (wb_ack_o) begin
      $display("ack_pulse at %0t", $time);
    end
    if (dut.i2c_core.r_busy || dut.i2c_core.ll_i2c_stb || dut.i2c_core.ll_i2c_cyc || dut.i2c_core.mstate != 0) begin
      $display("core: cyc=%b stb=%b we=%b state=%0d busy=%b ack=%b err=%b tx=%02x", dut.i2c_core.ll_i2c_cyc, dut.i2c_core.ll_i2c_stb, dut.i2c_core.ll_i2c_we, dut.i2c_core.mstate, dut.i2c_core.r_busy, dut.i2c_core.ll_i2c_ack, dut.i2c_core.ll_i2c_err, dut.i2c_core.ll_i2c_tx_data);
    end
    if (dut.i2c_core.lowlvl.state != 0 || dut.i2c_core.lowlvl.i_stb || dut.i2c_core.lowlvl.i_cyc) begin
      $display("ll: state=%0d zclk=%b o_scl=%b o_sda=%b ack=%b busy=%b err=%b", dut.i2c_core.lowlvl.state, dut.i2c_core.lowlvl.zclk, dut.i2c_core.lowlvl.o_scl, dut.i2c_core.lowlvl.o_sda, dut.i2c_core.lowlvl.o_ack, dut.i2c_core.lowlvl.o_busy, dut.i2c_core.lowlvl.o_err);
    end
  end
  initial begin
    #20 rst = 0;
    wb_adr_i = 32'h10005000;
    wb_dat_i = 32'h00000001 | (32'h3c << 17);
    wb_we_i = 1; wb_cyc_i = 1; wb_stb_i = 1;
    repeat (4) @(posedge clk);
    wb_we_i = 0; wb_cyc_i = 0; wb_stb_i = 0;
    #5000;
    $display("stb=%b ack=%b scl=%b sda=%b", dut.i_wb_stb, wb_ack_o, i2c_scl, i2c_sda);
    $finish;
  end
endmodule
