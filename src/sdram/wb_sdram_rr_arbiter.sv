`default_nettype none

module wb_sdram_rr_arbiter (
    input  wire        clk,
    input  wire        rst,

    input  wire [31:0] m0_adr_i,
    input  wire [31:0] m0_dat_i,
    input  wire [3:0]  m0_sel_i,
    input  wire        m0_we_i,
    input  wire        m0_cyc_i,
    input  wire        m0_stb_i,
    output reg  [31:0] m0_dat_o,
    output reg         m0_ack_o,

    input  wire [31:0] m1_adr_i,
    input  wire [31:0] m1_dat_i,
    input  wire [3:0]  m1_sel_i,
    input  wire        m1_we_i,
    input  wire        m1_cyc_i,
    input  wire        m1_stb_i,
    output reg  [31:0] m1_dat_o,
    output reg         m1_ack_o,

    output reg  [31:0] s_adr_o,
    output reg  [31:0] s_dat_o,
    output reg  [3:0]  s_sel_o,
    output reg         s_we_o,
    output reg         s_cyc_o,
    output reg         s_stb_o,
    input  wire [31:0] s_dat_i,
    input  wire        s_ack_i,

    output reg  [2:0]  dbg_state_o,
    output reg         dbg_last_grant_o,
    output reg         dbg_rr_prefer_m1_o,
    output reg         dbg_m0_req_o,
    output reg         dbg_m1_req_o,
    output reg  [31:0] dbg_m0_grant_count_o,
    output reg  [31:0] dbg_m1_grant_count_o,
    output reg  [31:0] dbg_m0_stall_count_o,
    output reg  [31:0] dbg_m1_stall_count_o,
    output reg  [31:0] dbg_ack_count_o
);

    localparam [2:0] ST_IDLE       = 3'd0;
    localparam [2:0] ST_M0         = 3'd1;
    localparam [2:0] ST_M1         = 3'd2;
    localparam [2:0] ST_M0_RELEASE = 3'd3;
    localparam [2:0] ST_M1_RELEASE = 3'd4;

    reg [2:0] state = ST_IDLE;
    reg       rr_prefer_m1 = 1'b0;

    wire m0_req = m0_cyc_i && m0_stb_i;
    wire m1_req = m1_cyc_i && m1_stb_i;

    always @(posedge clk) begin
        if (rst) begin
            state <= ST_IDLE;
            rr_prefer_m1 <= 1'b0;
            m0_ack_o <= 1'b0;
            m1_ack_o <= 1'b0;
            m0_dat_o <= 32'd0;
            m1_dat_o <= 32'd0;
            s_adr_o <= 32'd0;
            s_dat_o <= 32'd0;
            s_sel_o <= 4'd0;
            s_we_o <= 1'b0;
            s_cyc_o <= 1'b0;
            s_stb_o <= 1'b0;
            dbg_state_o <= ST_IDLE;
            dbg_last_grant_o <= 1'b0;
            dbg_rr_prefer_m1_o <= 1'b0;
            dbg_m0_req_o <= 1'b0;
            dbg_m1_req_o <= 1'b0;
            dbg_m0_grant_count_o <= 32'd0;
            dbg_m1_grant_count_o <= 32'd0;
            dbg_m0_stall_count_o <= 32'd0;
            dbg_m1_stall_count_o <= 32'd0;
            dbg_ack_count_o <= 32'd0;
        end else begin
            m0_ack_o <= 1'b0;
            m1_ack_o <= 1'b0;
            dbg_state_o <= state;
            dbg_rr_prefer_m1_o <= rr_prefer_m1;
            dbg_m0_req_o <= m0_req;
            dbg_m1_req_o <= m1_req;

            if (m0_req && (state != ST_M0) && (state != ST_M0_RELEASE))
                dbg_m0_stall_count_o <= dbg_m0_stall_count_o + 32'd1;
            if (m1_req && (state != ST_M1) && (state != ST_M1_RELEASE))
                dbg_m1_stall_count_o <= dbg_m1_stall_count_o + 32'd1;

            case (state)
                ST_IDLE: begin
                    s_cyc_o <= 1'b0;
                    s_stb_o <= 1'b0;

                    if (m0_req && (!m1_req || !rr_prefer_m1)) begin
                        s_adr_o <= m0_adr_i;
                        s_dat_o <= m0_dat_i;
                        s_sel_o <= m0_sel_i;
                        s_we_o <= m0_we_i;
                        s_cyc_o <= 1'b1;
                        s_stb_o <= 1'b1;
                        dbg_last_grant_o <= 1'b0;
                        dbg_m0_grant_count_o <= dbg_m0_grant_count_o + 32'd1;
                        state <= ST_M0;
                    end else if (m1_req) begin
                        s_adr_o <= m1_adr_i;
                        s_dat_o <= m1_dat_i;
                        s_sel_o <= m1_sel_i;
                        s_we_o <= m1_we_i;
                        s_cyc_o <= 1'b1;
                        s_stb_o <= 1'b1;
                        dbg_last_grant_o <= 1'b1;
                        dbg_m1_grant_count_o <= dbg_m1_grant_count_o + 32'd1;
                        state <= ST_M1;
                    end
                end

                ST_M0: begin
                    s_adr_o <= m0_adr_i;
                    s_dat_o <= m0_dat_i;
                    s_sel_o <= m0_sel_i;
                    s_we_o <= m0_we_i;
                    s_cyc_o <= m0_cyc_i;
                    s_stb_o <= m0_stb_i;
                    if (s_ack_i) begin
                        m0_dat_o <= s_dat_i;
                        m0_ack_o <= 1'b1;
                        dbg_ack_count_o <= dbg_ack_count_o + 32'd1;
                        s_cyc_o <= 1'b0;
                        s_stb_o <= 1'b0;
                        rr_prefer_m1 <= 1'b1;
                        state <= ST_M0_RELEASE;
                    end else if (!m0_req) begin
                        s_cyc_o <= 1'b0;
                        s_stb_o <= 1'b0;
                        rr_prefer_m1 <= 1'b1;
                        state <= ST_IDLE;
                    end
                end

                ST_M1: begin
                    s_adr_o <= m1_adr_i;
                    s_dat_o <= m1_dat_i;
                    s_sel_o <= m1_sel_i;
                    s_we_o <= m1_we_i;
                    s_cyc_o <= m1_cyc_i;
                    s_stb_o <= m1_stb_i;
                    if (s_ack_i) begin
                        m1_dat_o <= s_dat_i;
                        m1_ack_o <= 1'b1;
                        dbg_ack_count_o <= dbg_ack_count_o + 32'd1;
                        s_cyc_o <= 1'b0;
                        s_stb_o <= 1'b0;
                        rr_prefer_m1 <= 1'b0;
                        state <= ST_M1_RELEASE;
                    end else if (!m1_req) begin
                        s_cyc_o <= 1'b0;
                        s_stb_o <= 1'b0;
                        rr_prefer_m1 <= 1'b0;
                        state <= ST_IDLE;
                    end
                end

                ST_M0_RELEASE: begin
                    s_cyc_o <= 1'b0;
                    s_stb_o <= 1'b0;
                    if (!m0_req) begin
                        state <= ST_IDLE;
                    end
                end

                ST_M1_RELEASE: begin
                    s_cyc_o <= 1'b0;
                    s_stb_o <= 1'b0;
                    if (!m1_req) begin
                        state <= ST_IDLE;
                    end
                end

                default: begin
                    state <= ST_IDLE;
                    s_cyc_o <= 1'b0;
                    s_stb_o <= 1'b0;
                end
            endcase
        end
    end

endmodule
