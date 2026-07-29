module wb_timer #(
    parameter ADDR = 32'h1000_0010,
    parameter CLK_HZ = 25_000_000
)
(
    input  wire        clk,
    input  wire        rst,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,

    output reg [31:0]  wb_dat_o,
    output reg         wb_ack_o
);

    localparam integer TICKS_PER_MS = CLK_HZ / 1000;

    reg [31:0] tick_div = 32'd0;
    reg [31:0] ms_counter = 32'd0;

    always @(posedge clk) begin
        if (rst) begin
            tick_div <= 32'd0;
            ms_counter <= 32'd0;
        end else begin
            if (tick_div == (TICKS_PER_MS - 1)) begin
                tick_div <= 32'd0;
                ms_counter <= ms_counter + 32'd1;
            end else begin
                tick_div <= tick_div + 32'd1;
            end

            // Optional write access to set current millisecond counter.
            if (wb_cyc_i && wb_stb_i && wb_we_i && (wb_adr_i == ADDR) && wb_sel_i[0])
                ms_counter <= wb_dat_i;
        end
    end

    always @(posedge clk) begin
        wb_ack_o <= wb_cyc_i && wb_stb_i;

        if (wb_cyc_i && wb_stb_i)
            wb_dat_o <= ms_counter;
    end

endmodule
