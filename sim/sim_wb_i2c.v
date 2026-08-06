`timescale 1ns/1ps

module sim_wb_i2c;
    localparam [31:0] I2C0_BASE = 32'h10005000;

    reg clk = 0;
    reg rst = 0;

    reg [31:0] wb_adr_i = 0;
    reg [31:0] wb_dat_i = 0;
    reg [3:0]  wb_sel_i = 4'hF;
    reg        wb_we_i = 0;
    reg        wb_cyc_i = 0;
    reg        wb_stb_i = 0;

    wire [31:0] wb_dat_o;
    wire        wb_ack_o;

    tri1 i2c_scl;
    tri1 i2c_sda;

    pullup(i2c_scl);
    pullup(i2c_sda);

    wb_i2c #(
        .ADDR(I2C0_BASE),
        .DEFAULT_PRESCALE(16'd62)
    ) dut (
        .clk(clk),
        .rst(rst),
        .wb_adr_i(wb_adr_i),
        .wb_dat_i(wb_dat_i),
        .wb_dat_o(wb_dat_o),
        .wb_sel_i(wb_sel_i),
        .wb_we_i(wb_we_i),
        .wb_cyc_i(wb_cyc_i),
        .wb_stb_i(wb_stb_i),
        .wb_ack_o(wb_ack_o),
        .i2c_scl(i2c_scl),
        .i2c_sda(i2c_sda)
    );

    always #5 clk = ~clk;

    task wb_write32;
        input [31:0] reg_offset;
        input [31:0] data;
        begin
            @(negedge clk);
            wb_adr_i = I2C0_BASE + reg_offset;
            wb_dat_i = data;
            wb_we_i = 1;
            wb_cyc_i = 1;
            wb_stb_i = 1;
            @(negedge clk);
            wb_stb_i = 0;
            wb_cyc_i = 0;
            wb_we_i = 0;
            @(negedge clk);
        end
    endtask

    initial begin
        $dumpfile("sim/sim_wb_i2c.vcd");
        $dumpvars(0, sim_wb_i2c);

        rst = 1;
        repeat (4) @(posedge clk);
        rst = 0;
        repeat (4) @(posedge clk);

        // Prescale: 62 -> 100 kHz @ 25 MHz
        wb_write32(32'h18, 32'h0000003E);
        wb_write32(32'h1C, 32'h00000000);

        // Address register: 0x3C
        wb_write32(32'h08, 32'h0000003C);

        // Start + write
        wb_write32(32'h0C, 32'h00000005);

        // Data byte
        wb_write32(32'h10, 32'h000000AA);

        // Stop
        wb_write32(32'h0C, 32'h00000010);

        repeat (300) @(posedge clk);
        $display("Simulation complete");
        $finish;
    end
endmodule
