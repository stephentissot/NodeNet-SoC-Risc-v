`timescale 1ns/1ps

module tb_wb_led;

    // ── Clock ────────────────────────────────────────────────────────────────
    reg clk = 0;
    always #20 clk = ~clk;  // 25 MHz (period = 40 ns)

    // ── Reset ────────────────────────────────────────────────────────────────
    reg rst = 1;
    initial begin #200 rst = 0; end

    // ── Wishbone signals ──────────────────────────────────────────────────────
    reg  [31:0] wb_adr = 0;
    reg  [31:0] wb_dat = 0;
    reg  [3:0]  wb_sel = 0;
    reg         wb_we  = 0;
    reg         wb_cyc = 0;
    reg         wb_stb = 0;
    wire [31:0] wb_dat_o;
    wire        wb_ack;
    wire        led0_pin;
    wire        led1_pin;

    // ── DUT: LED0 (active-low, default OFF, 10-cycle blink for fast sim) ─────
    wb_led #(
        .ADDR        (32'h10000004),
        .ACTIVE_LOW  (1),
        .DEFAULT_STATE(0),
        .BLINK_CYCLES(10)       // very short for simulation
    ) dut0 (
        .clk      (clk),
        .rst      (rst),
        .wb_adr_i (wb_adr),
        .wb_dat_i (wb_dat),
        .wb_sel_i (wb_sel),
        .wb_we_i  (wb_we),
        .wb_cyc_i (wb_cyc),
        .wb_stb_i (wb_stb),
        .wb_dat_o (wb_dat_o),
        .wb_ack_o (wb_ack),
        .led      (led0_pin)
    );

    // ── Wishbone write task ───────────────────────────────────────────────────
    task wb_write;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk); #1;
            wb_adr = addr;
            wb_dat = data;
            wb_sel = 4'hF;
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

    // ── Wishbone read task ────────────────────────────────────────────────────
    task wb_read;
        input  [31:0] addr;
        output [31:0] rdata;
        begin
            @(posedge clk); #1;
            wb_adr = addr;
            wb_sel = 4'hF;
            wb_we  = 0;
            wb_cyc = 1;
            wb_stb = 1;
            @(posedge clk);
            while (!wb_ack) @(posedge clk);
            rdata = wb_dat_o;
            #1;
            wb_cyc = 0;
            wb_stb = 0;
        end
    endtask

    // ── Test sequence ─────────────────────────────────────────────────────────
    integer i;
    reg [31:0] rdata;

    initial begin
        $dumpfile("sim/tb_wb_led.vcd");
        $dumpvars(0, tb_wb_led);

        // Wait for reset to deassert
        @(negedge rst);
        repeat(5) @(posedge clk);
        $display("=== Reset done ===");
        $display("led0_pin (initial, should be 1=OFF): %b", led0_pin);

        // ── TEST 1: Set default ON (logical 1 → pin LOW for active-low) ──────
        $display("\n--- TEST 1: Set default ON ---");
        wb_write(32'h10000004, 32'h6);  // bit1=SET_DEFAULT, bit2=value(1=ON)
        repeat(3) @(posedge clk);
        $display("led0_pin after On() (expect 0=ON): %b", led0_pin);

        // ── TEST 2: Set default OFF ───────────────────────────────────────────
        $display("\n--- TEST 2: Set default OFF ---");
        wb_write(32'h10000004, 32'h2);  // bit1=SET_DEFAULT, bit2=0=OFF
        repeat(3) @(posedge clk);
        $display("led0_pin after Off() (expect 1=OFF): %b", led0_pin);

        // ── TEST 3: Readback status ───────────────────────────────────────────
        $display("\n--- TEST 3: Readback ---");
        wb_read(32'h10000004, rdata);
        $display("readback=0x%08h  [bit0=state:%b bit1=busy:%b bit2=default:%b]",
                 rdata, rdata[0], rdata[1], rdata[2]);

        // ── TEST 4: Trigger blink (BLINK_CYCLES=10 → should blink for 10 clk) -
        $display("\n--- TEST 4: Blink trigger (default BLINK_CYCLES=10) ---");
        wb_write(32'h10000004, 32'h1);  // bit0=TRIGGER, cycles=0→use default(10)
        $display("led0_pin after trigger (expect 0=ON): %b", led0_pin);
        repeat(5) @(posedge clk);
        wb_read(32'h10000004, rdata);
        $display("status mid-blink: busy=%b led=%b", rdata[1], rdata[0]);
        repeat(8) @(posedge clk);
        $display("led0_pin after blink (expect 1=OFF): %b", led0_pin);

        // ── TEST 5: Trigger blink with explicit cycle count ───────────────────
        $display("\n--- TEST 5: Blink with explicit 20 cycles ---");
        // make_blink_cmd(20) = (20 << 3) | 1 = 161 = 0xA1
        wb_write(32'h10000004, 32'hA1);
        repeat(5) @(posedge clk);
        $display("led0_pin mid-blink (expect 0=ON): %b", led0_pin);
        repeat(20) @(posedge clk);
        $display("led0_pin after blink (expect 1=OFF): %b", led0_pin);

        $display("\n=== Simulation complete ===");
        $finish;
    end

    // ── Watchdog ──────────────────────────────────────────────────────────────
    initial begin
        #1_000_000;
        $display("TIMEOUT");
        $finish;
    end

endmodule
