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

    input  wire [31:0] m2_adr_i,
    input  wire [31:0] m2_dat_i,
    input  wire [3:0]  m2_sel_i,
    input  wire        m2_we_i,
    input  wire        m2_cyc_i,
    input  wire        m2_stb_i,
    output reg  [31:0] m2_dat_o,
    output reg         m2_ack_o,

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
    output reg         dbg_m2_req_o,
    output reg  [31:0] dbg_m0_grant_count_o,
    output reg  [31:0] dbg_m1_grant_count_o,
    output reg  [31:0] dbg_m2_grant_count_o,
    output reg  [31:0] dbg_m0_stall_count_o,
    output reg  [31:0] dbg_m1_stall_count_o,
    output reg  [31:0] dbg_m2_stall_count_o,
    output reg  [31:0] dbg_ack_count_o
);

    localparam [2:0] ST_IDLE       = 3'd0;
    localparam [2:0] ST_ACTIVE     = 3'd1;
    localparam [2:0] ST_RELEASE    = 3'd2;

    reg [2:0] state = ST_IDLE;
    reg [1:0] rr_start = 2'd0;
    reg [1:0] active_master = 2'd0;

    wire m0_req = m0_cyc_i && m0_stb_i;
    wire m1_req = m1_cyc_i && m1_stb_i;
    wire m2_req = m2_cyc_i && m2_stb_i;

    reg       grant_valid;
    reg [1:0] grant_master;

    always @(*) begin
        grant_valid = 1'b1;
        grant_master = 2'd0;

        case (rr_start)
            2'd0: begin
                if (m0_req) begin
                    grant_master = 2'd0;
                end else if (m1_req) begin
                    grant_master = 2'd1;
                end else if (m2_req) begin
                    grant_master = 2'd2;
                end else begin
                    grant_valid = 1'b0;
                end
            end
            2'd1: begin
                if (m1_req) begin
                    grant_master = 2'd1;
                end else if (m2_req) begin
                    grant_master = 2'd2;
                end else if (m0_req) begin
                    grant_master = 2'd0;
                end else begin
                    grant_valid = 1'b0;
                end
            end
            default: begin
                if (m2_req) begin
                    grant_master = 2'd2;
                end else if (m0_req) begin
                    grant_master = 2'd0;
                end else if (m1_req) begin
                    grant_master = 2'd1;
                end else begin
                    grant_valid = 1'b0;
                end
            end
        endcase
    end

    always @(posedge clk) begin
        if (rst) begin
            state <= ST_IDLE;
            rr_start <= 2'd0;
            active_master <= 2'd0;
            m0_ack_o <= 1'b0;
            m1_ack_o <= 1'b0;
            m2_ack_o <= 1'b0;
            m0_dat_o <= 32'd0;
            m1_dat_o <= 32'd0;
            m2_dat_o <= 32'd0;
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
            dbg_m2_req_o <= 1'b0;
            dbg_m0_grant_count_o <= 32'd0;
            dbg_m1_grant_count_o <= 32'd0;
            dbg_m2_grant_count_o <= 32'd0;
            dbg_m0_stall_count_o <= 32'd0;
            dbg_m1_stall_count_o <= 32'd0;
            dbg_m2_stall_count_o <= 32'd0;
            dbg_ack_count_o <= 32'd0;
        end else begin
            m0_ack_o <= 1'b0;
            m1_ack_o <= 1'b0;
            m2_ack_o <= 1'b0;
            dbg_state_o <= state;
            dbg_rr_prefer_m1_o <= (rr_start == 2'd1);
            dbg_m0_req_o <= m0_req;
            dbg_m1_req_o <= m1_req;
            dbg_m2_req_o <= m2_req;

            if (m0_req && !((state == ST_ACTIVE || state == ST_RELEASE) && active_master == 2'd0))
                dbg_m0_stall_count_o <= dbg_m0_stall_count_o + 32'd1;
            if (m1_req && !((state == ST_ACTIVE || state == ST_RELEASE) && active_master == 2'd1))
                dbg_m1_stall_count_o <= dbg_m1_stall_count_o + 32'd1;
            if (m2_req && !((state == ST_ACTIVE || state == ST_RELEASE) && active_master == 2'd2))
                dbg_m2_stall_count_o <= dbg_m2_stall_count_o + 32'd1;

            case (state)
                ST_IDLE: begin
                    s_cyc_o <= 1'b0;
                    s_stb_o <= 1'b0;

                    if (grant_valid) begin
                        active_master <= grant_master;
                        case (grant_master)
                            2'd0: begin
                                s_adr_o <= m0_adr_i;
                                s_dat_o <= m0_dat_i;
                                s_sel_o <= m0_sel_i;
                                s_we_o <= m0_we_i;
                                dbg_last_grant_o <= 1'b0;
                                dbg_m0_grant_count_o <= dbg_m0_grant_count_o + 32'd1;
                            end
                            2'd1: begin
                                s_adr_o <= m1_adr_i;
                                s_dat_o <= m1_dat_i;
                                s_sel_o <= m1_sel_i;
                                s_we_o <= m1_we_i;
                                dbg_last_grant_o <= 1'b1;
                                dbg_m1_grant_count_o <= dbg_m1_grant_count_o + 32'd1;
                            end
                            default: begin
                                s_adr_o <= m2_adr_i;
                                s_dat_o <= m2_dat_i;
                                s_sel_o <= m2_sel_i;
                                s_we_o <= m2_we_i;
                                dbg_last_grant_o <= 1'b0;
                                dbg_m2_grant_count_o <= dbg_m2_grant_count_o + 32'd1;
                            end
                        endcase
                        s_cyc_o <= 1'b1;
                        s_stb_o <= 1'b1;
                        state <= ST_ACTIVE;
                    end
                end

                ST_ACTIVE: begin
                    case (active_master)
                        2'd0: begin
                            s_adr_o <= m0_adr_i;
                            s_dat_o <= m0_dat_i;
                            s_sel_o <= m0_sel_i;
                            s_we_o <= m0_we_i;
                            s_cyc_o <= m0_cyc_i;
                            s_stb_o <= m0_stb_i;
                        end
                        2'd1: begin
                            s_adr_o <= m1_adr_i;
                            s_dat_o <= m1_dat_i;
                            s_sel_o <= m1_sel_i;
                            s_we_o <= m1_we_i;
                            s_cyc_o <= m1_cyc_i;
                            s_stb_o <= m1_stb_i;
                        end
                        default: begin
                            s_adr_o <= m2_adr_i;
                            s_dat_o <= m2_dat_i;
                            s_sel_o <= m2_sel_i;
                            s_we_o <= m2_we_i;
                            s_cyc_o <= m2_cyc_i;
                            s_stb_o <= m2_stb_i;
                        end
                    endcase

                    if (s_ack_i) begin
                        case (active_master)
                            2'd0: begin
                                m0_dat_o <= s_dat_i;
                                m0_ack_o <= 1'b1;
                            end
                            2'd1: begin
                                m1_dat_o <= s_dat_i;
                                m1_ack_o <= 1'b1;
                            end
                            default: begin
                                m2_dat_o <= s_dat_i;
                                m2_ack_o <= 1'b1;
                            end
                        endcase
                        dbg_ack_count_o <= dbg_ack_count_o + 32'd1;
                        s_cyc_o <= 1'b0;
                        s_stb_o <= 1'b0;
                        rr_start <= (active_master == 2'd2) ? 2'd0 : (active_master + 2'd1);
                        state <= ST_RELEASE;
                    end else if ((active_master == 2'd0 && !m0_req) ||
                                 (active_master == 2'd1 && !m1_req) ||
                                 (active_master == 2'd2 && !m2_req)) begin
                        s_cyc_o <= 1'b0;
                        s_stb_o <= 1'b0;
                        rr_start <= (active_master == 2'd2) ? 2'd0 : (active_master + 2'd1);
                        state <= ST_IDLE;
                    end
                end

                ST_RELEASE: begin
                    s_cyc_o <= 1'b0;
                    s_stb_o <= 1'b0;
                    if ((active_master == 2'd0 && !m0_req) ||
                        (active_master == 2'd1 && !m1_req) ||
                        (active_master == 2'd2 && !m2_req)) begin
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
