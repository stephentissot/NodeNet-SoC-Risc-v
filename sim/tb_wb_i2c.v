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
    tri1 i2c_scl;
    tri1 i2c_sda;
    pullup(i2c_scl);
    pullup(i2c_sda);

    wire scl_mon = (i2c_scl === 1'bz || i2c_scl === 1'bx) ? 1'b1 : i2c_scl;
    wire sda_mon = (i2c_sda === 1'bz || i2c_sda === 1'bx) ? 1'b1 : i2c_sda;

    integer scl_edges = 0;
    integer sda_edges = 0;
    localparam integer LONG_WAIT = 20000;
    reg [16:0] prev_bit_state = 17'h0;

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

    task wb_read;
        input [31:0] addr;
        output [31:0] data;
        begin
            @(posedge clk); #1;
            wb_adr = addr;
            wb_dat = 0;
            wb_we  = 0;
            wb_cyc = 1;
            wb_stb = 1;
            @(posedge clk);
            while (!wb_ack) @(posedge clk);
            #1;
            data = wb_dat_o;
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
        repeat(LONG_WAIT) @(posedge clk);

        // setClock(100000 Hz)
        $display("[init] setClock(100000)");
        mmio_write(32'h18, 32'd62);
        mmio_write(32'h1C, 32'd0);
        repeat(LONG_WAIT) @(posedge clk);

        // beginTransmission(0x3C)
        $display("[fw] beginTransmission(0x3C)");
        mmio_write(32'h08, 32'h3C);
        repeat(LONG_WAIT) @(posedge clk);

        // write(0x3C)
        $display("[fw] write(0x3C)");
        mmio_write(32'h10, 32'h3C);   // data byte
        mmio_write(32'h0C, 32'h90);   // start + write

        // Regression: the wrapper should expose the I2C core status register
        // through offset 0x00 so firmware waitForDone() sees TIP/busy bits.
        begin
            reg [31:0] status_val;
            integer i;
            status_val = 32'h0;
            for (i = 0; i < 16; i = i + 1) begin
                wb_read(I2C_BASE + 32'h00, status_val);
                $display("[check] status @ 0x00 = 0x%08x", status_val);
                if ((status_val & 32'h02) != 0) begin
                    $display("[check] observed TIP after START+WRITE");
                    i = 16;
                end else begin
                    repeat(10) @(posedge clk);
                end
            end
            if ((status_val & 32'h02) == 0) begin
                $display("ERROR: status register did not report TIP after START+WRITE");
                $finish;
            end
        end

        repeat(LONG_WAIT) @(posedge clk);

        // endTransmission()
        $display("[fw] endTransmission()");
        mmio_write(32'h0C, 32'h40);   // stop
        repeat(LONG_WAIT) @(posedge clk);

        if (scl_edges >= 6) begin
            $display("=== simulation complete: observed %0d SCL edges and %0d SDA edges ===", scl_edges, sda_edges);
        end else begin
            $display("=== simulation incomplete: only %0d SCL edges and %0d SDA edges ===", scl_edges, sda_edges);
        end
        $finish;
    end

    always @(posedge scl_mon or negedge scl_mon) begin
        scl_edges = scl_edges + 1;
        $display("[%0t] SCL edge #%0d -> %b", $time, scl_edges, scl_mon);
    end

    always @(posedge sda_mon or negedge sda_mon) begin
        sda_edges = sda_edges + 1;
        $display("[%0t] SDA edge #%0d -> %b", $time, sda_edges, sda_mon);
    end

    always @(posedge clk) begin
        if (dut.i2c_inst.bit_controller.cnt == 0) begin
            $display("[%0t] prescaler wrap: cnt=0 clk_en=%b cmd=%b bit_state=%b", $time,
                dut.i2c_inst.bit_controller.clk_en,
                dut.i2c_inst.byte_controller.core_cmd,
                dut.i2c_inst.bit_controller.c_state);
        end
    end

    initial begin
        $dumpfile("sim/tb_wb_i2c.vcd");
        $dumpvars(0, tb_wb_i2c);
    end

    initial begin
        #5_000_000_000;
        $display("TIMEOUT");
        $finish;
    end

endmodule
