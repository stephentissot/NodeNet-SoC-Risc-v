module wb_rom #(
    parameter ADDR_WIDTH = 14
)
(
    input  wire        clk,

    // Wishbone slave
    input  wire [31:0] wbs_adr_i,
    input  wire        wbs_cyc_i,
    input  wire        wbs_stb_i,

    output reg  [31:0] wbs_dat_o,
    output reg         wbs_ack_o
);

    reg [7:0] rom [0:(1<<(ADDR_WIDTH+2))-1];

    initial begin
        $display("Loading firmware...");
        $readmemh("src/firmware/build/nodenet_riscv.hex", rom);
    end


    always @(posedge clk)
    begin
        wbs_ack_o <= wbs_cyc_i && wbs_stb_i;

        if(wbs_cyc_i && wbs_stb_i)
            wbs_dat_o <= {
                rom[wbs_adr_i[ADDR_WIDTH+1:0] + 2'd3],
                rom[wbs_adr_i[ADDR_WIDTH+1:0] + 2'd2],
                rom[wbs_adr_i[ADDR_WIDTH+1:0] + 2'd1],
                rom[wbs_adr_i[ADDR_WIDTH+1:0] + 2'd0]
            };
    end

endmodule