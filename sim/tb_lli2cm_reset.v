`timescale 1ns/1ps

`timescale 1ns/1ps

module tb_lli2cm_reset;
    reg clk = 0;
    reg reset = 1;
    reg cyc = 0;
    reg stb = 0;
    reg we = 0;
    reg [7:0] data = 8'h00;
    reg scl = 1;
    reg sda = 1;

    wire ack;
    wire busy;
    wire err;
    wire [7:0] rx_data;
    wire scl_out;
    wire sda_out;

    lli2cm #(
        .TICKBITS(6),
        .CLOCKS_PER_TICK(8),
        .PROGRAMMABLE_RATE(1'b0)
    ) dut (
        .i_clk(clk),
        .i_reset(reset),
        .i_clocks(6'd8),
        .i_cyc(cyc),
        .i_stb(stb),
        .i_we(we),
        .i_data(data),
        .o_ack(ack),
        .o_busy(busy),
        .o_err(err),
        .o_data(rx_data),
        .i_scl(scl),
        .i_sda(sda),
        .o_scl(scl_out),
        .o_sda(sda_out),
        .o_dbg()
    );

    always #5 clk = ~clk;

    initial begin
        #25 reset = 0;
        #40;
        if (busy !== 1'b0) begin
            $display("FAIL: busy should be idle after reset");
            $finish(1);
        end
        if (scl_out !== 1'b1 || sda_out !== 1'b1) begin
            $display("FAIL: reset should leave lines high-idle");
            $finish(1);
        end

        cyc = 1;
        stb = 1;
        we = 1;
        data = 8'hA5;
        #100;

        if (busy !== 1'b1) begin
            $display("FAIL: transaction did not start");
            $finish(1);
        end

        $display("PASS");
        $finish(0);
    end
endmodule
