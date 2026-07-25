module wb_ram #(
    parameter ADDR_WIDTH = 14
)
(
    input  wire        clk,

    input  wire [31:0] wbs_adr_i,
    input  wire [31:0] wbs_dat_i,
    input  wire [3:0]  wbs_sel_i,
    input  wire        wbs_we_i,
    input  wire        wbs_cyc_i,
    input  wire        wbs_stb_i,

    output reg  [31:0] wbs_dat_o,
    output reg         wbs_ack_o
);

    reg [31:0] ram [0:(1<<ADDR_WIDTH)-1];
    reg [ADDR_WIDTH-1:0] word_addr;
    reg [31:0] write_data;

    always @(posedge clk)
    begin
        wbs_ack_o <= wbs_cyc_i && wbs_stb_i;
        word_addr = wbs_adr_i[ADDR_WIDTH+1:2];

        if (wbs_cyc_i && wbs_stb_i)
        begin
            wbs_dat_o <= ram[word_addr];

            if (wbs_we_i)
            begin
                write_data = ram[word_addr];

                if (wbs_sel_i[0])
                    write_data[7:0] <= wbs_dat_i[7:0];
                if (wbs_sel_i[1])
                    write_data[15:8] <= wbs_dat_i[15:8];
                if (wbs_sel_i[2])
                    write_data[23:16] <= wbs_dat_i[23:16];
                if (wbs_sel_i[3])
                    write_data[31:24] <= wbs_dat_i[31:24];

                ram[word_addr] <= write_data;
            end
        end
    end

endmodule