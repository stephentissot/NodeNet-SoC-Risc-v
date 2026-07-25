module wb_led #(
    parameter ADDR = 32'h10000000
)
(
    input  wire        clk,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,

    output reg [31:0]  wb_dat_o,
    output reg         wb_ack_o,

    output reg         led
);

    always @(posedge clk)
    begin
        wb_ack_o <= wb_cyc_i && wb_stb_i;
        wb_dat_o <= {31'd0, led};

        if(wb_cyc_i && wb_stb_i && wb_we_i && (wb_adr_i == ADDR) && wb_sel_i[0])
            led <= wb_dat_i[0];
    end
endmodule