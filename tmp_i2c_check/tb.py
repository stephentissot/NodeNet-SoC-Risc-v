import subprocess, pathlib
work = pathlib.Path('tmp_i2c_check')
src = r'''
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
  wb_i2c_zipcpu #(.ADDR(32'h10005000), .TICKBITS(6'd20), .CLOCKS_PER_TICK(20'd1000), .MEM_ADDR_BITS(8), .LITTLE_ENDIAN(1'b1)) dut (
    .clk(clk), .rst(rst),
    .wb_adr_i(wb_adr_i), .wb_dat_i(wb_dat_i), .wb_dat_o(wb_dat_o),
    .wb_sel_i(wb_sel_i), .wb_we_i(wb_we_i), .wb_cyc_i(wb_cyc_i), .wb_stb_i(wb_stb_i),
    .wb_ack_o(wb_ack_o),
    .i2c_scl(i2c_scl), .i2c_sda(i2c_sda)
  );
  always #5 clk = ~clk;
  initial begin
    #20 rst = 0;
    wb_adr_i = 32'h10005000;
    wb_dat_i = 32'h00000001 | (32'h3c << 17);
    wb_we_i = 1; wb_cyc_i = 1; wb_stb_i = 1;
    #10;
    wb_we_i = 0; wb_cyc_i = 0; wb_stb_i = 0;
    #5000;
    $display("scl=%b sda=%b", i2c_scl, i2c_sda);
    $finish;
  end
endmodule
''';
(work/'tb.v').write_text(src, encoding='utf-8')
files = [work/'tb.v', pathlib.Path('src/wbi2c/wb_i2c_zipcpu.sv'), pathlib.Path('src/wbi2c/wbi2cmaster.v'), pathlib.Path('src/wbi2c/lli2cm.v')]
cmd = ['iverilog.exe','-g2012','-o',str(work/'tb.vvp')] + [str(p) for p in files]
print('running', ' '.join(cmd))
res = subprocess.run(cmd, capture_output=True, text=True)
print(res.stdout)
print(res.stderr)
if res.returncode != 0:
    raise SystemExit(res.returncode)
res = subprocess.run(['vvp.exe', str(work/'tb.vvp')], capture_output=True, text=True)
print(res.stdout)
print(res.stderr)
