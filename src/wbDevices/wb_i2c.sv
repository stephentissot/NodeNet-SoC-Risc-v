// wb_i2c.sv
// Wishbone B.4 (32-bit) wrapper around Alex Forencich's i2c_master_wbs_8
//
// i2c_master_wbs_8 has an 8-bit Wishbone slave interface with 3-bit address
// (8 registers).  This wrapper adapts it to our 32-bit CPU bus:
//   - Data:    wb_dat_i/o[7:0] forwarded; upper bits zero-padded on read
//   - Address: wb_adr_i[4:2] selects the register (4-byte word stride)
//
// Register map (from base ADDR, 4-byte stride):
//   +0x00  Status      R:  [0]=busy  [1]=bus_cont  [2]=bus_act  [3]=miss_ack
//                      W:  write 1 to [3] to clear miss_ack
//   +0x04  FIFO Stat   R:  [0]=cmd_empty [1]=cmd_full [2]=cmd_ovf
//                          [3]=wr_empty  [4]=wr_full  [5]=wr_ovf
//                          [6]=rd_empty  [7]=rd_full
//   +0x08  Cmd Addr    W:  [6:0] = 7-bit I2C address for next command
//   +0x0C  Command     W:  [0]=start [1]=read [2]=write [4]=stop
//   +0x10  Data        W:  push to TX FIFO  /  R: pop from RX FIFO
//   +0x14  (reserved)
//   +0x18  Prescale Lo W:  prescale[7:0]   (prescale = Fclk / (FI2C * 4))
//   +0x1C  Prescale Hi W:  prescale[15:8]
//
// Prescale examples (25 MHz clock):
//   100 kHz →  62   (25_000_000 / (100_000 * 4))
//   400 kHz →  15   (25_000_000 / (400_000 * 4))
//
// SCL/SDA: open-drain tristate — connect directly to inout FPGA pins with
// external pullup resistors (typically 4.7 kΩ to 3.3 V).
//
// Source: https://github.com/alexforencich/verilog-i2c  (MIT licence)

`default_nettype none

module wb_i2c #(
    parameter [31:0] ADDR             = 32'h1000_5000,
    parameter [15:0] DEFAULT_PRESCALE = 16'd62,   // 100 kHz @ 25 MHz
    parameter        CMD_FIFO_DEPTH   = 32,
    parameter        WRITE_FIFO_DEPTH = 32,
    parameter        READ_FIFO_DEPTH  = 32
)(
    input  wire        clk,
    input  wire        rst,

    // Wishbone B.4 slave (32-bit bus)
    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output wire [31:0] wb_dat_o,    // 8-bit result zero-extended to 32 bits
    input  wire [3:0]  wb_sel_i,    // Not used (byte-granularity not needed)
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output wire        wb_ack_o,

    // I2C open-drain interface (SCL/SDA with external pullup)
    inout  wire        i2c_scl,
    inout  wire        i2c_sda
);

    // ─── Tristate signals from i2c_master core ───────────────────────────────
    // _t = tristate enable: 1 → release (high-Z, external pullup wins)
    //                       0 → drive _o onto the pin
    wire i2c_scl_i, i2c_scl_o, i2c_scl_t;
    wire i2c_sda_i, i2c_sda_o, i2c_sda_t;

    // Open-drain: drive low when _t=0 (and _o=0); release when _t=1
    assign i2c_scl   = i2c_scl_t ? 1'bz : i2c_scl_o;
    assign i2c_sda   = i2c_sda_t ? 1'bz : i2c_sda_o;
    assign i2c_scl_i = i2c_scl;
    assign i2c_sda_i = i2c_sda;

    // ─── 8-bit data from inner module, zero-extended ─────────────────────────
    wire [7:0] i2c_dat_o_8;
    assign wb_dat_o = {24'h0, i2c_dat_o_8};
    // Restore the real Wishbone handshake: the CPU should only proceed when
    // the inner I2C core has acknowledged the transfer. The unconditional ACK
    // above was only a temporary debug hack and can hide transaction issues.
//    wire       wbs_ack_internal;
//    assign wb_ack_o = wbs_ack_internal;
       
    // ─── Instantiate Alex Forencich's 8-bit Wishbone I2C master ─────────────
    // Address mapping: wb_adr_i[4:2] selects register 0–7 (4-byte word stride)
    i2c_master_wbs_8 #(
        .DEFAULT_PRESCALE(DEFAULT_PRESCALE),
        .FIXED_PRESCALE  (0),
        .CMD_FIFO        (1),
        .CMD_FIFO_DEPTH  (CMD_FIFO_DEPTH),
        .WRITE_FIFO      (1),
        .WRITE_FIFO_DEPTH(WRITE_FIFO_DEPTH),
        .READ_FIFO       (1),
        .READ_FIFO_DEPTH (READ_FIFO_DEPTH)
    ) i2c_inst (
        .clk        (clk),
        .rst        (rst),

        // Wishbone 8-bit slave
        .wbs_adr_i  (wb_adr_i[4:2]),   // bits[4:2] = register index 0-7
        .wbs_dat_i  (wb_dat_i[7:0]),   // lower byte of CPU write
        .wbs_dat_o  (i2c_dat_o_8),
        .wbs_we_i   (wb_we_i),
        .wbs_stb_i  (wb_stb_i),
        .wbs_ack_o  (wb_ack_o),
        .wbs_cyc_i  (wb_cyc_i),

        // I2C physical
        .i2c_scl_i  (i2c_scl_i),
        .i2c_scl_o  (i2c_scl_o),
        .i2c_scl_t  (i2c_scl_t),
        .i2c_sda_i  (i2c_sda_i),
        .i2c_sda_o  (i2c_sda_o),
        .i2c_sda_t  (i2c_sda_t)
    );

endmodule

`default_nettype wire
