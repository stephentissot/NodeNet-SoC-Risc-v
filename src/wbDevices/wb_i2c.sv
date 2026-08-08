// wb_i2c.sv
// Wishbone B.4 (32-bit) wrapper around Alex Forencich's i2c_master_wbs_16
//
// i2c_master_wbs_16 has a 16-bit Wishbone slave interface with 3-bit address
// using byte offsets (0x00, 0x02, 0x04, 0x06).  This wrapper adapts it to our
// 32-bit CPU bus with 4-byte word stride:
//   - Data:    wb_dat_i/o[15:0] forwarded; upper bits zero-padded on read
//   - Address: wb_adr_i[3:1] maps +0x00/+0x04/+0x08/+0x0C to 0/2/4/6
//   - Select:  wb_sel_i[1:0] forwarded to preserve 16-bit atomic accesses
//
// Register map (from base ADDR, 4-byte stride):
//   +0x00  Status   (inner +0x00)
//   +0x04  Command  (inner +0x02)
//   +0x08  Data     (inner +0x04)
//   +0x0C  Prescale (inner +0x06)
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
    output wire [31:0] wb_dat_o,    // 16-bit result zero-extended to 32 bits
    input  wire [3:0]  wb_sel_i,
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

    // ─── 16-bit data from inner module, zero-extended ────────────────────────
    wire [15:0] i2c_dat_o_16;
    assign wb_dat_o = {16'h0, i2c_dat_o_16};

    // ─── Instantiate Alex Forencich's 16-bit Wishbone I2C master ────────────
    // 32-bit CPU word offsets map as:
    //   +0x00 -> inner +0x00 (status)
    //   +0x04 -> inner +0x02 (command)
    //   +0x08 -> inner +0x04 (data)
    //   +0x0C -> inner +0x06 (prescale)
    i2c_master_wbs_16 #(
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

        // Wishbone 16-bit slave
        .wbs_adr_i  (wb_adr_i[3:1]),
        .wbs_dat_i  (wb_dat_i[15:0]),
        .wbs_dat_o  (i2c_dat_o_16),
        .wbs_we_i   (wb_we_i),
        .wbs_sel_i  (wb_sel_i[1:0]),
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
