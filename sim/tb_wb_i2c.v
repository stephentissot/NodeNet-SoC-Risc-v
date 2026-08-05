`timescale 1ns/1ps

module tb_wb_i2c;

    // ── Clock ───────────────────────────────────────────────────────────────
    reg clk = 0;
    always #20 clk = ~clk;  // 25 MHz

    // ── Reset ───────────────────────────────────────────────────────────────
    reg rst = 1;
    initial begin
        #200 rst = 0;
    end

    // ── Wishbone signals ───────────────────────────────────────────────────
    reg  [31:0] wb_adr = 0;
    reg  [31:0] wb_dat = 0;
    reg  [3:0]  wb_sel = 4'hF;
    reg         wb_we  = 0;
    reg         wb_cyc = 0;
    reg         wb_stb = 0;
    wire [31:0] wb_dat_o;
    wire        wb_ack;

    // ── I2C pins ───────────────────────────────────────────────────────────
    wire i2c_scl;
    wire i2c_sda;
    pullup(i2c_scl);
    pullup(i2c_sda);

    // ── DUT ────────────────────────────────────────────────────────────────
    wb_i2c dut (
        .clk      (clk),
        .rst      (rst),
        .wb_adr_i (wb_adr),
        .wb_dat_i (wb_dat),
        .wb_dat_o (wb_dat_o),
        .wb_sel_i (wb_sel),
        .wb_we_i  (wb_we),
        .wb_cyc_i (wb_cyc),
        .wb_stb_i (wb_stb),
        .wb_ack_o (wb_ack),
        .i2c_scl  (i2c_scl),
        .i2c_sda  (i2c_sda)
    );

    // ── Wishbone write task ────────────────────────────────────────────────
    task wb_write;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk); #1;
            wb_adr = addr;
            wb_dat = data;
            wb_we  = 1;
            wb_cyc = 1;
            wb_stb = 1;
            @(posedge clk);
            while (!wb_ack) @(posedge clk);
            #1;
            wb_cyc = 0;
            wb_stb = 0;
            wb_we  = 0;
        end
    endtask

    // ── Firmware-like MMIO helpers ────────────────────────────────────────
    localparam [31:0] I2C_BASE = 32'h10005000;
    task mmio_write;
        input [31:0] offset;
        input [31:0] data;
        begin
            wb_write(I2C_BASE + offset, data);
        end
    endtask

    // ── Simple trace on SCL/SDA changes ───────────────────────────────────
    initial begin
        $display("=== tb_wb_i2c: firmware-like I2C sequence ===");
        @(negedge rst);
        repeat(5) @(posedge clk);

        // begin(): prescale + stop + clear sticky status
        $display("[init] begin()");
        mmio_write(32'h18, 32'd62);   // prescale low
        mmio_write(32'h1C, 32'd0);    // prescale high
        mmio_write(32'h00, 32'h08);   // clear miss-ack
        mmio_write(32'h0C, 32'h40);   // stop
        repeat(10) @(posedge clk);

        // setClock(100000 Hz)
        $display("[init] setClock(100000)");
        mmio_write(32'h18, 32'd62);
        mmio_write(32'h1C, 32'd0);
        repeat(10) @(posedge clk);

        // beginTransmission(0x3C)
        $display("[fw] beginTransmission(0x3C)");
        mmio_write(32'h08, 32'h3C);
        repeat(6) @(posedge clk);

        // write(0x3C)
        $display("[fw] write(0x3C)");
        mmio_write(32'h10, 32'h3C);   // data byte
        mmio_write(32'h0C, 32'h90);   // start + write
        repeat(30) @(posedge clk);

        // endTransmission()
        $display("[fw] endTransmission()");
        mmio_write(32'h0C, 32'h40);   // stop
        repeat(20) @(posedge clk);

        $display("=== simulation complete ===");
        $finish;
    end

    always @(i2c_scl or i2c_sda) begin
        $display("[%0t] scl=%b sda=%b", $time, i2c_scl, i2c_sda);
    end

    initial begin
        $monitor("[%0t] core_adr=%b core_dat=%h we=%b stb=%b cyc=%b ack=%b enabled=%b ctr=%h cr=%h txr=%h cmd=%h core_ack=%b al=%b byte_state=%b bit_state=%b clk_en=%b cnt=%d scl_oen=%b sda_oen=%b", $time,
            dut.core_adr, dut.core_dat_i, dut.core_we, dut.core_stb, dut.core_cyc, dut.core_ack, dut.core_enabled,
            dut.i2c_inst.ctr, dut.i2c_inst.cr, dut.i2c_inst.txr, dut.i2c_inst.byte_controller.core_cmd,
            dut.i2c_inst.core_ack, dut.i2c_inst.i2c_al, dut.i2c_inst.byte_controller.c_state,
            dut.i2c_inst.bit_controller.c_state, dut.i2c_inst.bit_controller.clk_en, dut.i2c_inst.bit_controller.cnt,
            dut.i2c_inst.scl_padoen_o, dut.i2c_inst.sda_padoen_o);
    end

    initial begin
        $dumpfile("sim/tb_wb_i2c.vcd");
        $dumpvars(0, tb_wb_i2c);
    end

    initial begin
        #1_000_000;
        $display("TIMEOUT");
        $finish;
    end

endmodule
