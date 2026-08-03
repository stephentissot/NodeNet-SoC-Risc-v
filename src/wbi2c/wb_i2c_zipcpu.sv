`default_nettype none

module wb_i2c_zipcpu #(
    parameter [31:0] ADDR = 32'h1000_5000,
    parameter [5:0] TICKBITS = 6'd20,
    parameter [19:0] CLOCKS_PER_TICK = 20'd1000,
    parameter MEM_ADDR_BITS = 8,
    parameter LITTLE_ENDIAN = 1'b1
)(
    input  wire        clk,
    input  wire        rst,

    // Wishbone B.4 slave (32-bit bus)
    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output reg  [31:0] wb_dat_o,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output wire        wb_ack_o,

    // I2C open-drain interface
    inout  wire        i2c_scl,
    inout  wire        i2c_sda
);

    wire i_wb_cyc;
    wire i_wb_stb;
    wire i_wb_we;
    wire [(MEM_ADDR_BITS-2):0] i_wb_addr;
    wire [31:0] i_wb_data;
    wire [3:0] i_wb_sel;
    wire o_wb_stall;
    wire o_wb_ack;
    wire [31:0] o_wb_data;
    wire [31:0] wb_addr_offset;

    wire i2c_scl_i;
    wire i2c_sda_i;
    wire i2c_scl_o;
    wire i2c_sda_o;

    assign i_wb_cyc  = wb_cyc_i;
    assign i_wb_stb  = wb_stb_i;
    assign i_wb_we   = wb_we_i;
    assign wb_addr_offset = wb_adr_i - ADDR;
    assign i_wb_addr = wb_addr_offset[(MEM_ADDR_BITS-1):2];
    assign i_wb_data = wb_dat_i;
    assign i_wb_sel  = wb_sel_i;

    assign wb_ack_o  = o_wb_ack;
    assign wb_dat_o  = o_wb_data;

    // Open-drain I2C pins: logic 1 releases the line, logic 0 drives it low.
    assign i2c_scl = i2c_scl_o ? 1'bz : 1'b0;
    assign i2c_sda = i2c_sda_o ? 1'bz : 1'b0;
    assign i2c_scl_i = i2c_scl;
    assign i2c_sda_i = i2c_sda;

    wbi2cmaster #(
        .CONSTANT_SPEED(1'b0),
        .READ_ONLY(1'b0),
        .LITTLE_ENDIAN(LITTLE_ENDIAN),
        .TICKBITS(TICKBITS),
        .CLOCKS_PER_TICK(CLOCKS_PER_TICK),
        .MEM_ADDR_BITS(MEM_ADDR_BITS)
    ) i2c_core (
        .i_clk(clk),
        .i_reset(rst),

        .i_wb_cyc(i_wb_cyc),
        .i_wb_stb(i_wb_stb),
        .i_wb_we(i_wb_we),
        .i_wb_addr(i_wb_addr),
        .i_wb_data(i_wb_data),
        .i_wb_sel(i_wb_sel),

        .o_wb_stall(o_wb_stall),
        .o_wb_ack(o_wb_ack),
        .o_wb_data(o_wb_data),

        .i_i2c_scl(i2c_scl_i),
        .i_i2c_sda(i2c_sda_i),
        .o_i2c_scl(i2c_scl_o),
        .o_i2c_sda(i2c_sda_o),

        .o_int(),
        .o_dbg()
    );

endmodule

`default_nettype wire
