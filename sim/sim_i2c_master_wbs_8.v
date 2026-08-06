`timescale 1ns/1ps

module sim_i2c_master_wbs_8;
    reg clk = 0;
    reg rst = 0;

    reg [2:0] wbs_adr_i = 0;
    reg [7:0] wbs_dat_i = 0;
    reg wbs_we_i = 0;
    reg wbs_stb_i = 0;
    reg wbs_cyc_i = 0;
    reg i2c_scl_i = 1;
    reg i2c_sda_i = 1;

    wire [7:0] wbs_dat_o;
    wire wbs_ack_o;
    wire i2c_scl_o;
    wire i2c_scl_t;
    wire i2c_sda_o;
    wire i2c_sda_t;

    integer cycle;

    i2c_master_wbs_8 #(
        .DEFAULT_PRESCALE(1),
        .FIXED_PRESCALE(0),
        .CMD_FIFO(1),
        .CMD_FIFO_DEPTH(32),
        .WRITE_FIFO(1),
        .WRITE_FIFO_DEPTH(32),
        .READ_FIFO(1),
        .READ_FIFO_DEPTH(32)
    ) dut (
        .clk(clk),
        .rst(rst),
        .wbs_adr_i(wbs_adr_i),
        .wbs_dat_i(wbs_dat_i),
        .wbs_dat_o(wbs_dat_o),
        .wbs_we_i(wbs_we_i),
        .wbs_stb_i(wbs_stb_i),
        .wbs_ack_o(wbs_ack_o),
        .wbs_cyc_i(wbs_cyc_i),
        .i2c_scl_i(i2c_scl_i),
        .i2c_scl_o(i2c_scl_o),
        .i2c_scl_t(i2c_scl_t),
        .i2c_sda_i(i2c_sda_i),
        .i2c_sda_o(i2c_sda_o),
        .i2c_sda_t(i2c_sda_t)
    );

    always #5 clk = ~clk;

    task wb_write;
        input [2:0] addr;
        input [7:0] data;
        begin
            @(negedge clk);
            wbs_adr_i = addr;
            wbs_dat_i = data;
            wbs_we_i = 1;
            wbs_stb_i = 1;
            wbs_cyc_i = 1;
            @(negedge clk);
            wbs_stb_i = 0;
            wbs_cyc_i = 0;
            wbs_we_i = 0;
            @(negedge clk);
        end
    endtask

    initial begin
        $dumpfile("sim/sim_i2c_master_wbs_8.vcd");
        $dumpvars(0, sim_i2c_master_wbs_8);

        rst = 1;
        repeat (4) @(posedge clk);
        rst = 0;
        repeat (4) @(posedge clk);

        // prescale
        wb_write(3, 8'h01);
        wb_write(7, 8'h01);

        // address 0x50
        wb_write(2, 8'h50);

        // start+write
        wb_write(3, 8'h05);

        // data
        wb_write(4, 8'hAA);

        // stop
        wb_write(3, 8'h10);

        repeat (200) @(posedge clk);
        $finish;
    end
endmodule
